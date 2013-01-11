// $Id$

#include "tao/Dynamic_TP/DTP_Thread_Pool.h"

#if defined (TAO_HAS_CORBA_MESSAGING) && TAO_HAS_CORBA_MESSAGING != 0

#if ! defined (__ACE_INLINE__)
#include "tao/Dynamic_TP/DTP_Thread_Pool.inl"
#endif /* __ACE_INLINE__ */

#include "tao/Exception.h"
#include "tao/ORB_Core.h"
#include "tao/ORB_Core_TSS_Resources.h"
#include "tao/TSS_Resources.h"
#include "tao/ORB.h"
#include "tao/Acceptor_Registry.h"
#include "tao/debug.h"
#include "tao/LF_Follower.h"
#include "tao/Leader_Follower.h"
#include "ace/Auto_Ptr.h"

TAO_BEGIN_VERSIONED_NAMESPACE_DECL

TAO_DTP_New_Leader_Generator::TAO_DTP_New_Leader_Generator (
  TAO_DTP_Thread_Pool &p)
  : pool_ (p)
{
}

void
TAO_DTP_New_Leader_Generator::no_leaders_available (void)
{
  this->pool_.new_dynamic_thread ();
}

TAO_DTP_Thread_Pool_Threads::TAO_DTP_Thread_Pool_Threads (TAO_DTP_Thread_Pool &p)
  : ACE_Task_Base (p.manager ().orb_core ().thr_mgr ()),
    pool_ (p)
{
}

int
TAO_DTP_Thread_Pool_Threads::svc (void)
{
  TAO_ORB_Core &orb_core = this->pool_.manager ().orb_core ();
  if (orb_core.has_shutdown ())
    return 0;

  try
    {
      // Do the work
      this->run (orb_core);
    }
  catch (const ::CORBA::Exception& ex)
    {
      // No point propagating this exception.  Print it out.
      ACE_ERROR ((LM_ERROR,
                  "orb->run() raised exception for thread %t\n"));

      ex._tao_print_exception ("");
    }

  return 0;
}

int
TAO_DTP_Thread_Pool_Threads::run (TAO_ORB_Core &orb_core)
{
  CORBA::ORB_ptr orb = orb_core.orb ();
  // A timeout is specified, run the ORB in an idle loop, if we
  // don't handle any operations for the given timeout we just
  // exit the loop and this thread ends itself.
  ACE_Time_Value tv (this->pool_.dynamic_thread_time ());

  while (!orb_core.has_shutdown ())
    {
      bool has_work = orb->work_pending (tv);
      if (!has_work && this->pool_.above_minimum ())
        {
          // we've timed out, but the pool is not yet at the minimum
          break;
        }

      // Run the ORB for the specified timeout, this prevents looping
      // between work_pending/handle_events
      tv = this->pool_.dynamic_thread_time ();
      orb->run (tv);
      // Reset the idle timeout
      tv = this->pool_.dynamic_thread_time ();
    }

  if (TAO_debug_level > 7)
    {
      ACE_DEBUG ((LM_DEBUG,
                  ACE_TEXT ("TAO Process %P Pool %d Thread %t\n")
                  ACE_TEXT ("Current number of dynamic threads left = %d; ")
                  ACE_TEXT ("DTP worker thread is ending!\n"),
                  this->pool_.id (),
                  this->thr_count () - 1));
    }

  return 0;
}

bool
TAO_DTP_Thread_Pool::above_minimum (void)
{
  return this->definition_.min_threads_ > 0 &&
    (int)this->threads_.thr_count () > this->definition_.min_threads_;
}

bool
TAO_DTP_Thread_Pool::new_dynamic_thread (void)
{
  // Note that we are checking this condition below without the lock
  // held.
  if (this->definition_.max_threads_ > 0 &&
      (int)this->threads_.thr_count () >= this->definition_.max_threads_)
    return false;

  ACE_GUARD_RETURN (TAO_SYNCH_MUTEX,
                    mon,
                    this->lock_,
                    false);

  if (!this->manager_.orb_core ().has_shutdown () && !this->shutdown_ &&
      (this->definition_.max_threads_ == -1 ||
       (int)this->threads_.thr_count () < this->definition_.max_threads_))
    {
      if (TAO_debug_level > 7)
        ACE_DEBUG ((LM_DEBUG,
                    ACE_TEXT ("TAO Process %P Pool %d Thread %t\n")
                    ACE_TEXT ("Current number of threads = %d; ")
                    ACE_TEXT ("min threads = %d; max threads = %d\n")
                    ACE_TEXT ("No leaders available; DTP creating new leader!\n"),
                    this->id_,
                    this->threads_.thr_count (),
                    this->definition_.min_threads_,
                    this->definition_.max_threads_));

      if (this->create_threads_i (1, THR_BOUND | THR_DETACHED))
        {
          if (TAO_debug_level > 0)
            {
              ACE_ERROR ((LM_ERROR,
                          ACE_TEXT ("Pool %d Thread %t: ")
                          ACE_TEXT ("cannot create dynamic thread\n"),
                          this->id_));
            }
          return false;
        }
    }

  return true;
}

CORBA::ULong
TAO_DTP_Thread_Pool::current_threads (void) const
{
  ACE_GUARD_RETURN (TAO_SYNCH_MUTEX,
                    mon,
                    this->lock_,
                    0);

  return this->threads_.thr_count ();
}

int
TAO_DTP_Thread_Pool::create_initial_threads (void)
{
  ACE_GUARD_RETURN (TAO_SYNCH_MUTEX,
                    mon,
                    this->lock_,
                    0);

  // Create initial threads.
  // first, create the minimum number of threads as static
  // if the min threads count is -1 that means all threads are static

  size_t count = (size_t)this->definition_.init_threads_;
  size_t extra = 0;
  if (this->definition_.min_threads_ != -1)
    {
      extra = count - (size_t) this->definition_.min_threads_;
      count = (size_t) this->definition_.min_threads_;
    }

  if (TAO_debug_level > 7)
    {
      ACE_DEBUG ((LM_DEBUG,
                  ACE_TEXT ("(%P|%t) DTP_Thread_Pool::create_initial_threads ")
                  ACE_TEXT ("Creating %d static and %d dynamic threads\n"),
                  count, extra));
    }

  int result = this->create_threads_i (count, THR_NEW_LWP | THR_JOINABLE);
  if (result != -1 && extra > 0)
    {
      result = this->create_threads_i (extra, THR_BOUND | THR_DETACHED);
    }
  return result;
}

int
TAO_DTP_Thread_Pool::create_dynamic_threads (size_t count)
{
  ACE_GUARD_RETURN (TAO_SYNCH_MUTEX,
                    mon,
                    this->lock_,
                    0);

  return this->create_threads_i (count, THR_BOUND | THR_DETACHED);
}

int
TAO_DTP_Thread_Pool::create_threads_i (size_t count,
                                       long thread_flags)
{
  // Overwritten parameters.
  int force_active = 1;

  // Default parameters.
  int default_grp_id = -1;
  ACE_Task_Base *default_task = 0;
  ACE_hthread_t *default_thread_handles = 0;
  void **default_stack = 0;

  // Setting stack size.
  size_t *stack_size_array = 0;
  ACE_NEW_RETURN (stack_size_array,
                  size_t[count],
                  -1);
  size_t index;
  for (index = 0; index != count; ++index)
    stack_size_array[index] =
      this->definition_.stack_size_;

  // Make sure the dynamically created stack size array is properly
  // deleted.
  ACE_Auto_Basic_Array_Ptr<size_t> auto_stack_size_array (stack_size_array);

  TAO_ORB_Core &orb_core = manager_.orb_core ();

  long flags =
    thread_flags |
    orb_core.orb_params ()->thread_creation_flags ();

  int default_priority = 0;

  // Activate the threads.
  int result =
    this->threads_.activate (flags,
                             count,
                             force_active,
                             default_grp_id,
                             default_priority,
                             default_task,
                             default_thread_handles,
                             default_stack,
                             stack_size_array);
  return result;
}

TAO_DTP_Thread_Pool::TAO_DTP_Thread_Pool (TAO_DTP_Thread_Pool_Manager &manager,
                                          CORBA::ULong id,
                                          TAO_DTP_Definition &def)
  : manager_ (manager),
    id_ (id),
    shutdown_ (false),
    definition_ (def),
    threads_ (*this),
    new_thread_generator_ (*this)
{
  manager_.orb_core ().leader_follower ().set_new_leader_generator (&new_thread_generator_);
}

void
TAO_DTP_Thread_Pool::open (void)
{
  // Nothing to do for now
}

TAO_DTP_Thread_Pool::~TAO_DTP_Thread_Pool (void)
{
}

void
TAO_DTP_Thread_Pool::shutting_down (void)
{
  ACE_GUARD (TAO_SYNCH_MUTEX,
             mon,
             this->lock_);

  // We are shutting down, this way we are not creating any more new dynamic
  // threads
  this->shutdown_ = true;
}


void
TAO_DTP_Thread_Pool::wait (void)
{
  this->threads_.wait ();
}

#define TAO_THREAD_POOL_MANAGER_GUARD \
  ACE_GUARD_THROW_EX ( \
    TAO_SYNCH_MUTEX, \
    mon, \
    this->lock_, \
    CORBA::INTERNAL ( \
      CORBA::SystemException::_tao_minor_code ( \
        TAO_GUARD_FAILURE, \
        0), \
      CORBA::COMPLETED_NO));

TAO_DTP_Thread_Pool_Manager::TAO_DTP_Thread_Pool_Manager (TAO_ORB_Core &orb_core)
  : orb_core_ (orb_core),
    thread_pools_ (),
    thread_pool_id_counter_ (1),
    lock_ ()
{
}

TAO_DTP_Thread_Pool_Manager::~TAO_DTP_Thread_Pool_Manager (void)
{
  // Delete all the pools.
  for (THREAD_POOLS::ITERATOR iterator = this->thread_pools_.begin ();
       iterator != this->thread_pools_.end ();
       ++iterator)
    delete (*iterator).int_id_;
}


void
TAO_DTP_Thread_Pool_Manager::wait (void)
{
  for (THREAD_POOLS::ITERATOR iterator = this->thread_pools_.begin ();
       iterator != this->thread_pools_.end ();
       ++iterator)
    (*iterator).int_id_->wait ();
}

CORBA::ULong
TAO_DTP_Thread_Pool_Manager::create_threadpool (TAO_DTP_Definition &def)
{
  TAO_THREAD_POOL_MANAGER_GUARD;

  return this->create_threadpool_i (def);
}

void
TAO_DTP_Thread_Pool_Manager::destroy_threadpool (CORBA::ULong threadpool)
{
  TAO_DTP_Thread_Pool *tao_thread_pool = 0;

  // The guard is just for the map, don't do a wait inside the guard, because
  // during the wait other threads can try to access the thread pool manager
  // also, this can be one of the threads we are waiting for, which then
  // results in a deadlock
  {
    TAO_THREAD_POOL_MANAGER_GUARD;

    // Unbind the thread pool from the map.
    int const result = this->thread_pools_.unbind (threadpool, tao_thread_pool);

    // If the thread pool is not found in our map.
    if (result != 0)
      return; //throw RTCORBA::RTORB::InvalidThreadpool ();
  }

  // Mark the thread pool that we are shutting down.
  tao_thread_pool->shutting_down ();

  // Wait for the threads.
  tao_thread_pool->wait ();

  // Delete the thread pool.
  delete tao_thread_pool;

}

CORBA::ULong
TAO_DTP_Thread_Pool_Manager::create_threadpool_i (TAO_DTP_Definition &def)
{
  // Create the thread pool.
  TAO_DTP_Thread_Pool *thread_pool = 0;

  ACE_NEW_THROW_EX (thread_pool,
                    TAO_DTP_Thread_Pool (*this,
                                     this->thread_pool_id_counter_,
                                     def
                                    ),
                    CORBA::NO_MEMORY ());

  return this->create_threadpool_helper (thread_pool);
}

CORBA::ULong
TAO_DTP_Thread_Pool_Manager::create_threadpool_helper (TAO_DTP_Thread_Pool *thread_pool)
{
  // Make sure of safe deletion in case of errors.
  auto_ptr<TAO_DTP_Thread_Pool> safe_thread_pool (thread_pool);

  // Open the pool.
  thread_pool->open ();

  // Create the static threads.
  int result = thread_pool->create_initial_threads ();

  // Throw exception in case of errors.
  if (result != 0)
    {
      throw ::CORBA::INTERNAL
        (
         CORBA::SystemException::_tao_minor_code
         (
          0, //TAO_RTCORBA_THREAD_CREATION_LOCATION_CODE,
          errno),
        CORBA::COMPLETED_NO);
    }

  // Bind thread to internal table.
  result = this->thread_pools_.bind (this->thread_pool_id_counter_, thread_pool);

  TAO_ORB_Core_TSS_Resources &tss =
    *this->orb_core_.get_tss_resources ();
  // Associate the thread pool with the ORB for later retrieval
  tss.lane_ = thread_pool;

  //
  // Throw exception in case of errors.
  if (result != 0)
    throw ::CORBA::INTERNAL ();

  //
  // Success.
  //

  // No need to delete thread pool.
  safe_thread_pool.release ();

  // Return current counter and perform post-increment.
  return this->thread_pool_id_counter_++;
}

TAO_ORB_Core &
TAO_DTP_Thread_Pool_Manager::orb_core (void) const
{
  return this->orb_core_;
}

TAO_END_VERSIONED_NAMESPACE_DECL

#endif /* TAO_HAS_CORBA_MESSAGING && TAO_HAS_CORBA_MESSAGING != 0 */
