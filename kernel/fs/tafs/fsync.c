// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ext4/fsync.c
 *
 *  Copyright (C) 1993  Stephen Tweedie (sct@redhat.com)
 *  from
 *  Copyright (C) 1992  Remy Card (card@masi.ibp.fr)
 *                      Laboratoire MASI - Institut Blaise Pascal
 *                      Universite Pierre et Marie Curie (Paris VI)
 *  from
 *  linux/fs/minix/truncate.c   Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext4fs fsync primitive
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 *
 *  Removed unnecessary code duplication for little endian machines
 *  and excessive __inline__s.
 *        Andi Kleen, 1997
 *
 * Major simplications and cleanup - we only need to do the metadata, because
 * we can depend on generic_block_fdatasync() to sync the data blocks.
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>

#include "ext4.h"
#include "ext4_jbd2.h"

#include <trace/events/ext4.h>

/*
 * If we're not journaling and this is a just-created file, we have to
 * sync our parent directory (if it was freshly created) since
 * otherwise it will only be written by writeback, leaving a huge
 * window during which a crash may lose the file.  This may apply for
 * the parent directory's parent as well, and so on recursively, if
 * they are also freshly created.
 */
static int ext4_sync_parent(struct inode *inode)
{
	struct dentry *dentry, *next;
	int ret = 0;

	if (!ext4_test_inode_state(inode, EXT4_STATE_NEWENTRY))
		return 0;
	dentry = d_find_any_alias(inode);
	if (!dentry)
		return 0;
	while (ext4_test_inode_state(inode, EXT4_STATE_NEWENTRY)) {
		ext4_clear_inode_state(inode, EXT4_STATE_NEWENTRY);

		next = dget_parent(dentry);
		dput(dentry);
		dentry = next;
		inode = dentry->d_inode;

		/*
		 * The directory inode may have gone through rmdir by now. But
		 * the inode itself and its blocks are still allocated (we hold
		 * a reference to the inode via its dentry), so it didn't go
		 * through ext4_evict_inode()) and so we are safe to flush
		 * metadata blocks and the inode.
		 */
		ret = sync_mapping_buffers(inode->i_mapping);
		if (ret)
			break;
		ret = sync_inode_metadata(inode, 1);
		if (ret)
			break;
	}
	dput(dentry);
	return ret;
}

static int ext4_fsync_nojournal(struct inode *inode, bool datasync,
				bool *needs_barrier)
{
	int ret, err;

	ret = sync_mapping_buffers(inode->i_mapping);
	if (!(inode->i_state & I_DIRTY_ALL))
		return ret;
	if (datasync && !(inode->i_state & I_DIRTY_DATASYNC))
		return ret;


	err = sync_inode_metadata(inode, 1);
	if (!ret)
		ret = err;

	if (!ret)
		ret = ext4_sync_parent(inode);
	if (test_opt(inode->i_sb, BARRIER))
		*needs_barrier = true;

	return ret;
}

static int ext4_fsync_journal(struct inode *inode, bool datasync,
			     bool *needs_barrier)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	journal_t *journal = EXT4_SB(inode->i_sb)->s_journal;
	tid_t commit_tid = datasync ? ei->i_datasync_tid : ei->i_sync_tid;

	if (journal->j_flags & JBD2_BARRIER &&
	    !jbd2_trans_will_send_data_barrier(journal, commit_tid))
		*needs_barrier = true;

	return ext4_fc_commit(journal, commit_tid);
}



#ifdef TARAID

int TA_need_stat; //this parameter should passed by ioctl
/*
	如果in_stat为真，则说明当前fsync由一个正在统计脏块的fsync出发，因此当前fsync直接退出
*/
static int in_stat = 0;
static struct chunk_set written_chunks;
static struct chunk_set {
    sector_t chunks[1024]; // 假设最多有1024个chunk
    int count;
};

#endif

/*
 * Edited by zeyvier
 * Context: Enhancements in TA-RAID for ext4_sync_file
 *
 * The standard ext4_sync_file synchronizes a file's dirty data and ensures crash consistency
 * by additionally recording a WAL-like journal. In TA-RAID, we utilize the out-of-place update 
 * mechanism of SSDs to provide atomicity and order, which are crucial for crash consistency.
 *
 * To this end, we introduce transaction handling in the I/O stack. We offer interfaces such as 
 * start_tx and commit_tx for transaction control and use the task_struct 'current' to transfer
 * transaction semantics. We have extended task_struct with two members: _tx_id and _tx_flag,
 * whose usage is documented in /include/linux/fs.h.
 *
 * Regarding ext4_sync_file modifications: when the current process is not in a transaction, we 
 * treat the sync process as atomic, allocate a new transaction ID, and pass it to the bio_vec 
 * structure. If the current process is in a transaction (indicated by a non-zero current->_tx_id),
 * the tx_id is passed to the buffer_head structure during ext4_write_begin. Transactions can involve 
 * multiple files; upon commit_tx invocation, multiple fsyncs are triggered for atomicity and durability. 
 * The final fsync in a transaction sends a commit request to the lower layer of the I/O stack. For a 
 * transaction involving 'n' files, the TARAID_NO_CMT flag in current->_tx_flag is set to 1 until the last 
 * file, where it's set to 0, prompting the FS to pass a commit request to the lower layer.
 *
 * Future implementations may support interfaces like fbarrier(fatomic), with process control via 
 * current->_tx_id.
 */


/*
 * akpm: A new design for ext4_sync_file().
 *
 * This is only called from sys_fsync(), sys_fdatasync() and sys_msync().
 * There cannot be a transaction open by this task.
 * Another task could have dirtied this inode.  Its data can be in any
 * state in the journalling system.
 *
 * What we do is just kick off a commit and wait on it.  This will snapshot the
 * inode to disk.
 */
int ext4_sync_file(struct file *file, loff_t start, loff_t end, int datasync)
{
#ifdef TARAID
	TARAID_debug(KERN_INFO"TARAID:	TAFS-->sync\n");
	
	if(current->_txid == 0)
	{
		current->_tx_flag |= TARAID_SYNC_TX;
		current->_tx_flag |= TARAID_NEED_CMT;
		current->_txid = TARAID_alloc_new_txid(file->f_mapping->host);
	}

#endif
	
	int ret = 0, err;
	bool needs_barrier = false;
	struct inode *inode = file->f_mapping->host;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);

	if (unlikely(ext4_forced_shutdown(sbi)))
		return -EIO;

	ASSERT(ext4_journal_current_handle() == NULL);

	trace_ext4_sync_file_enter(file, datasync);

	if (sb_rdonly(inode->i_sb)) {
		/* Make sure that we read updated s_mount_flags value */
		smp_rmb();
		if (ext4_test_mount_flag(inode->i_sb, EXT4_MF_FS_ABORTED))
			ret = -EROFS;
		goto out;
	}
/*

//edited by zeyvier : flollowing code is written to statistic the I/O amount in fsync 

#ifdef TARAID
	if(TA_need_stat == 0 || in_stat != 0)
		goto writeWait;
	in_stat = 1;
	
	printk(KERN_INFO"TARAID: CROSS_TEST\n");
	written_chunks.count = 0;
	sector_t chunk_size = 512; //KB per chunk
	sector_t chunk = 0;
	struct ext4_map_blocks block_map;
	block_map.m_len = 1;
	sector_t start_blk = start >> inode->i_blkbits;
	sector_t end_blk = end >> inode->i_blkbits;
	int has_error = 0;
	printk(KERN_INFO"TARAID: CROSS_TEST Phy Block: \n");
	sector_t cur_blk;
	for(cur_blk = start_blk; cur_blk <= end_blk; cur_blk++){
		block_map.m_lblk = cur_blk;
		has_error = ext4_map_blocks(NULL, inode, &block_map, 0);
		printk(KERN_INFO"%lld ",block_map.m_pblk);
		chunk = block_map.m_pblk / (chunk_size * 2);

		int chunk_found = 0;
		int i;
        for (i = 0; i < written_chunks.count; i++) {
            if (written_chunks.chunks[i] == chunk) {
                chunk_found = 1;
                break;
            }
        }

        // 如果当前chunk不在集合中，记录它并增加计数
        if (!chunk_found) {
            written_chunks.chunks[written_chunks.count] = chunk;
            written_chunks.count++;
        }
		printk(KERN_INFO"\n different chunk:%lld \n",written_chunks.count);
	}
	if(has_error){
		printk(KERN_INFO"TARAID: CROSS_TEST get physical block number error\n");
	}
	in_stat = 0;

#endif

*/


writeWait:
	ret = file_write_and_wait_range(file, start, end);
	if (ret)
		goto out;

#ifdef TARAID
	TARAID_clear_tx();

#endif

	/*
	 * data=writeback,ordered:
	 *  The caller's filemap_fdatawrite()/wait will sync the data.
	 *  Metadata is in the journal, we wait for proper transaction to
	 *  commit here.
	 *
	 * data=journal:
	 *  filemap_fdatawrite won't do anything (the buffers are clean).
	 *  ext4_force_commit will write the file data into the journal and
	 *  will wait on that.
	 *  filemap_fdatawait() will encounter a ton of newly-dirtied pages
	 *  (they were dirtied by commit).  But that's OK - the blocks are
	 *  safe in-journal, which is all fsync() needs to ensure.
	 */
	if (!sbi->s_journal)
		ret = ext4_fsync_nojournal(inode, datasync, &needs_barrier);
	else if (ext4_should_journal_data(inode))
		ret = ext4_force_commit(inode->i_sb);
	else
		ret = ext4_fsync_journal(inode, datasync, &needs_barrier);

	if (needs_barrier) {
		err = blkdev_issue_flush(inode->i_sb->s_bdev);
		if (!ret)
			ret = err;
	}
out:
	err = file_check_and_advance_wb_err(file);
	if (ret == 0)
		ret = err;
	trace_ext4_sync_file_exit(inode, ret);
	return ret;
}
