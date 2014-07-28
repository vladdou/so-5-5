#include <iostream>
#include <thread>
#include <vector>
#include <cstdlib>
#include <numeric>
#include <mutex>
#include <condition_variable>
#include <deque>

#include <so_5/h/spinlocks.hpp>

#include <test/so_5/bench/benchmark_helpers.hpp>

template< class T >
class mpmc_ptr_queue_t
	{
	public :
		/*!
		 * Type of heavy synchronization object to wait for if
		 * queue is empty.
		 */
		class waiting_object_t
			{
				waiting_object_t( const waiting_object_t & ) = delete;

				waiting_object_t &
				operator=( const waiting_object_t & ) = delete;

				friend class mpmc_ptr_queue_t;

			public :
				inline
				waiting_object_t()
					{}

			private :
				//! Waiting for notification.
				inline void
				wait( std::unique_lock< std::mutex > & l )
					{
						m_condition.wait( l );
					}

				//! Make notification.
				inline void
				lock_and_notify()
					{
						std::lock_guard< std::mutex > l( m_mutex );
						m_condition.notify_one();
					}

			private :
				std::mutex m_mutex;
				std::condition_variable m_condition;
			};

		mpmc_ptr_queue_t()
			{
			}
		~mpmc_ptr_queue_t()
			{
			}

		//! Get next active queue.
		/*!
		 * \retval nullptr is the case of dispatcher shutdown.
		 */
		inline T *
		pop( waiting_object_t & wt_alarm )
			{
#if 0
				using hrc = std::chrono::high_resolution_clock;
				auto spin_stop_point = hrc::now() +
						std::chrono::microseconds( 500 );
#endif
				const std::size_t max_spins = 102400;
				std::size_t spins = 0;

				do
					{
						std::unique_lock< so_5::default_spinlock_t > lock( m_lock );
						++spins;

						if( !m_queue.empty() )
							{
								auto r = m_queue.front();
								m_queue.pop_front();
								return r;
							}

						if( max_spins < spins )
							{
								// Spin loop must be finished.
								// Next waiting must be on heavy waiting object.
								m_waiting_threads.push_back( &wt_alarm );

								std::unique_lock< std::mutex > wt_lock(
										wt_alarm.m_mutex );

								lock.unlock();
								wt_alarm.wait( wt_lock );
							}
						else
							{
								// Spin loop must be continued.
								lock.unlock();
								std::this_thread::yield();
							}
					}
				while( true );

				return nullptr;
			}

		//! Schedule execution of demands from the queue.
		void
		schedule( T * queue )
			{
				std::lock_guard< so_5::default_spinlock_t > lock( m_lock );

				m_queue.push_back( queue );

				if( !m_waiting_threads.empty() )
					pop_and_notify_one_waiting_thread();
			}

	private :
		//! Object's lock.
		so_5::default_spinlock_t m_lock;

		char cache1__[ 64 ];

		//! Queue object.
		std::deque< T * > m_queue;

		char cache2__[ 64 ];

		//! Waiting threads.
		std::deque< waiting_object_t * > m_waiting_threads;

		void
		pop_and_notify_one_waiting_thread()
			{
				waiting_object_t * wt_alarm = m_waiting_threads.front();
				m_waiting_threads.pop_front();

				wt_alarm->lock_and_notify();
			}
	};

struct delta_t
{
	std::size_t m_delta;
};

struct demand_t
{
	bool m_need_shutdown = false;
	delta_t * m_delta = nullptr;
	std::size_t m_processed = 0;
	char cache__[ 64 - sizeof(std::size_t) - sizeof(bool) - sizeof(delta_t*) ];
};

using queue_t = mpmc_ptr_queue_t< demand_t >;

void
thread_body( queue_t * queue )
{
	queue_t::waiting_object_t wo;

	demand_t * ptr;
	while( nullptr != (ptr = queue->pop( wo )) )
	{
		if( ptr->m_need_shutdown )
			return;

		ptr->m_processed += ptr->m_delta->m_delta;
		delete ptr->m_delta;
		ptr->m_delta = new delta_t{ 1 };

		queue->schedule( ptr );
	}
}

void
do_test( unsigned int test_time_seconds, unsigned int thread_count )
{
	queue_t queue;

	std::vector< demand_t > demands( thread_count, demand_t() );
	std::vector< demand_t > shutdown_demands( thread_count, demand_t() );

	std::vector< std::thread > threads;
	threads.reserve( thread_count );
	for( std::size_t i = 0; i != thread_count; ++i )
		threads.emplace_back( thread_body, &queue );

	benchmarker_t benchmarker;
	benchmarker.start();

	for( auto & d : demands )
	{
		d.m_delta = new delta_t { 1 };
		queue.schedule( &d );
	}

	std::this_thread::sleep_for( std::chrono::seconds( test_time_seconds ) );

	for( auto & sd : shutdown_demands )
	{
		sd.m_need_shutdown = true;
		queue.schedule( &sd );
	}

	for( auto & t : threads )
		t.join();

	std::size_t demands_total = std::accumulate( demands.begin(),
			demands.end(), static_cast< std::size_t >(0),
			[]( std::size_t a, const demand_t & b ) { return a + b.m_processed; } );

	benchmarker.finish_and_show_stats( demands_total, "demands" );
}

int main( int argc, char ** argv )
{
	unsigned int seconds = 5;
	unsigned int thread_count = std::thread::hardware_concurrency();

	if( 1 < argc )
		seconds = std::atoi( argv[ 1 ] );
	if( 2 < argc )
		thread_count = std::atoi( argv[ 2 ] );

	std::cout << "seconds: " << seconds << ", threads: " << thread_count
		<< "\n..." << std::endl;

	do_test( seconds, thread_count );
}

