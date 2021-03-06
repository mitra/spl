/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#ifndef _SPLAT_INTERNAL_H
#define _SPLAT_INTERNAL_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/elf.h>
#include <linux/limits.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/swap.h>
#include <linux/delay.h>

#include <asm/ioctls.h>
#include <asm/uaccess.h>
#include <stdarg.h>

#include <sys/callb.h>
#include <sys/condvar.h>
#include <sys/cred.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/kstat.h>
#include <sys/mutex.h>
#include <sys/random.h>
#include <sys/rwlock.h>
#include <sys/taskq.h>
#include <sys/thread.h>
#include <sys/time.h>
#include <sys/timer.h>
#include <sys/types.h>
#include <sys/kobj.h>
#include <sys/atomic.h>
#include <sys/list.h>
#include <sys/sunddi.h>
#include <linux/cdev.h>

#include "spl-device.h"
#include "splat-ctl.h"

#define SPLAT_SUBSYSTEM_INIT(type)                                      \
({      splat_subsystem_t *_sub_;                                       \
                                                                        \
        _sub_ = (splat_subsystem_t *)splat_##type##_init();             \
        if (_sub_ == NULL) {                                            \
                printk(KERN_ERR "splat: Error initializing: " #type "\n"); \
        } else {                                                        \
                spin_lock(&splat_module_lock);                          \
                list_add_tail(&(_sub_->subsystem_list),			\
		              &splat_module_list);			\
                spin_unlock(&splat_module_lock);                        \
        }                                                               \
})

#define SPLAT_SUBSYSTEM_FINI(type)                                      \
({      splat_subsystem_t *_sub_, *_tmp_;                               \
        int _id_, _flag_ = 0;                                           \
                                                                        \
	_id_ = splat_##type##_id();                                     \
        spin_lock(&splat_module_lock);                                  \
        list_for_each_entry_safe(_sub_, _tmp_,  &splat_module_list,	\
		                 subsystem_list) {			\
                if (_sub_->desc.id == _id_) {                           \
                        list_del_init(&(_sub_->subsystem_list));        \
                        spin_unlock(&splat_module_lock);                \
                        splat_##type##_fini(_sub_);                     \
			spin_lock(&splat_module_lock);			\
                        _flag_ = 1;                                     \
                }                                                       \
        }                                                               \
        spin_unlock(&splat_module_lock);                                \
                                                                        \
	if (!_flag_)                                                    \
                printk(KERN_ERR "splat: Error finalizing: " #type "\n"); \
})

#define SPLAT_TEST_INIT(sub, n, d, tid, func)				\
({      splat_test_t *_test_;                                           \
                                                                        \
	_test_ = (splat_test_t *)kmalloc(sizeof(*_test_), GFP_KERNEL);  \
        if (_test_ == NULL) {						\
		printk(KERN_ERR "splat: Error initializing: " n "/" #tid" \n");\
	} else {							\
		memset(_test_, 0, sizeof(*_test_));			\
		strncpy(_test_->desc.name, n, SPLAT_NAME_SIZE-1);	\
		strncpy(_test_->desc.desc, d, SPLAT_DESC_SIZE-1);	\
		_test_->desc.id = tid;					\
	        _test_->test = func;					\
		INIT_LIST_HEAD(&(_test_->test_list));			\
                spin_lock(&((sub)->test_lock));				\
                list_add_tail(&(_test_->test_list),&((sub)->test_list));\
                spin_unlock(&((sub)->test_lock));			\
        }								\
})

#define SPLAT_TEST_FINI(sub, tid)					\
({      splat_test_t *_test_, *_tmp_;                                   \
        int _flag_ = 0;							\
                                                                        \
        spin_lock(&((sub)->test_lock));					\
        list_for_each_entry_safe(_test_, _tmp_,				\
		                 &((sub)->test_list), test_list) {	\
                if (_test_->desc.id == tid) {                           \
                        list_del_init(&(_test_->test_list));		\
                        _flag_ = 1;                                     \
                }                                                       \
        }                                                               \
        spin_unlock(&((sub)->test_lock));				\
                                                                        \
	if (!_flag_)                                                    \
                printk(KERN_ERR "splat: Error finalizing: " #tid "\n");	\
})

typedef int (*splat_test_func_t)(struct file *, void *);

typedef struct splat_test {
	struct list_head test_list;
	splat_user_t desc;
	splat_test_func_t test;
} splat_test_t;

typedef struct splat_subsystem {
	struct list_head subsystem_list;/* List had to chain entries */
	splat_user_t desc;
	spinlock_t test_lock;
	struct list_head test_list;
} splat_subsystem_t;

#define SPLAT_INFO_BUFFER_SIZE		65536
#define SPLAT_INFO_BUFFER_REDZONE	256

typedef struct splat_info {
	spinlock_t info_lock;
	int info_size;
	char *info_buffer;
	char *info_head;	/* Internal kernel use only */
} splat_info_t;

#define sym2str(sym)			(char *)(#sym)

#define splat_print(file, format, args...)				\
({	splat_info_t *_info_ = (splat_info_t *)file->private_data;	\
	int _rc_;							\
									\
	ASSERT(_info_);							\
	ASSERT(_info_->info_buffer);					\
									\
	spin_lock(&_info_->info_lock);					\
									\
	/* Don't allow the kernel to start a write in the red zone */	\
	if ((int)(_info_->info_head - _info_->info_buffer) >		\
	    (SPLAT_INFO_BUFFER_SIZE - SPLAT_INFO_BUFFER_REDZONE)) {	\
		_rc_ = -EOVERFLOW;					\
	} else {							\
		_rc_ = sprintf(_info_->info_head, format, args);	\
		if (_rc_ >= 0)						\
			_info_->info_head += _rc_;			\
	}								\
									\
	spin_unlock(&_info_->info_lock);				\
	_rc_;								\
})

#define splat_vprint(file, test, format, args...)			\
	splat_print(file, "%*s: " format, SPLAT_NAME_SIZE, test, args)

#define splat_locked_test(lock, test)					\
({									\
	int _rc_;							\
	spin_lock(lock);						\
	_rc_ = (test) ? 1 : 0;						\
	spin_unlock(lock);						\
	_rc_;								\
})

splat_subsystem_t *splat_condvar_init(void);
splat_subsystem_t *splat_kmem_init(void);
splat_subsystem_t *splat_mutex_init(void);
splat_subsystem_t *splat_krng_init(void);
splat_subsystem_t *splat_rwlock_init(void);
splat_subsystem_t *splat_taskq_init(void);
splat_subsystem_t *splat_thread_init(void);
splat_subsystem_t *splat_time_init(void);
splat_subsystem_t *splat_vnode_init(void);
splat_subsystem_t *splat_kobj_init(void);
splat_subsystem_t *splat_atomic_init(void);
splat_subsystem_t *splat_list_init(void);
splat_subsystem_t *splat_generic_init(void);
splat_subsystem_t *splat_cred_init(void);

void splat_condvar_fini(splat_subsystem_t *);
void splat_kmem_fini(splat_subsystem_t *);
void splat_mutex_fini(splat_subsystem_t *);
void splat_krng_fini(splat_subsystem_t *);
void splat_rwlock_fini(splat_subsystem_t *);
void splat_taskq_fini(splat_subsystem_t *);
void splat_thread_fini(splat_subsystem_t *);
void splat_time_fini(splat_subsystem_t *);
void splat_vnode_fini(splat_subsystem_t *);
void splat_kobj_fini(splat_subsystem_t *);
void splat_atomic_fini(splat_subsystem_t *);
void splat_list_fini(splat_subsystem_t *);
void splat_generic_fini(splat_subsystem_t *);
void splat_cred_fini(splat_subsystem_t *);

int splat_condvar_id(void);
int splat_kmem_id(void);
int splat_mutex_id(void);
int splat_krng_id(void);
int splat_rwlock_id(void);
int splat_taskq_id(void);
int splat_thread_id(void);
int splat_time_id(void);
int splat_vnode_id(void);
int splat_kobj_id(void);
int splat_atomic_id(void);
int splat_list_id(void);
int splat_generic_id(void);
int splat_cred_id(void);

#endif /* _SPLAT_INTERNAL_H */
