/*
	SObjectizer 5.
*/

#include <so_5/disp/one_thread/h/pub.hpp>

#include <so_5/rt/h/disp.hpp>
#include <so_5/rt/h/environment.hpp>
#include <so_5/rt/h/send_functions.hpp>

#include <so_5/rt/stats/h/repository.hpp>
#include <so_5/rt/stats/h/messages.hpp>
#include <so_5/rt/stats/h/std_names.hpp>

#include <so_5/disp/reuse/work_thread/h/work_thread.hpp>

#include <so_5/disp/reuse/h/data_source_prefix_helpers.hpp>

#include <so_5/details/h/rollback_on_exception.hpp>

namespace so_5
{

namespace disp
{

namespace one_thread
{

namespace impl
{

namespace work_thread = so_5::disp::reuse::work_thread;
namespace stats = so_5::rt::stats;

//
// dispatcher_t
//

/*!
	\brief A dispatcher with the single working thread and an event queue.
*/
class dispatcher_t : public so_5::rt::dispatcher_t
	{
	public:
		dispatcher_t()
			:	m_data_source( m_work_thread )
			{}

		//! \name Implementation of so_5::rt::dispatcher methods.
		//! \{
		virtual void
		start( so_5::rt::environment_t & env ) override
			{
				m_data_source.start( env );

				so_5::details::do_with_rollback_on_exception(
						[this] { m_work_thread.start(); },
						[this] { m_data_source.stop(); } );
			}

		virtual void
		shutdown() override
			{
				m_work_thread.shutdown();
			}

		virtual void
		wait() override
			{
				m_work_thread.wait();

				m_data_source.stop();
			}

		virtual void
		set_data_sources_name_base(
			const std::string & name_base ) override
			{
				m_data_source.set_data_sources_name_base( name_base, this );
			}
		//! \}

		/*!
		 * \since v.5.4.0
		 * \brief Get a binding information for an agent.
		 */
		so_5::rt::event_queue_t *
		get_agent_binding()
			{
				return m_work_thread.get_agent_binding();
			}

	private:
		/*!
		 * \since v.5.5.4
		 * \brief Type of data source for run-time monitoring.
		 */
		class data_source_t : public stats::source_t
			{
				//! SObjectizer Environment to work in.
				so_5::rt::environment_t * m_env = { nullptr };

				//! Prefix for dispatcher-related data.
				stats::prefix_t m_base_prefix;
				//! Prefix for working thread-related data.
				stats::prefix_t m_work_thread_prefix;

				//! Working thread of the dispatcher.
				work_thread::work_thread_t & m_work_thread;

			public :
				data_source_t(
					work_thread::work_thread_t & work_thread )
					:	m_work_thread( work_thread )
					{}

				~data_source_t()
					{
						if( m_env )
							stop();
					}

				virtual void
				distribute( const so_5::rt::mbox_t & mbox )
					{
//FIXME: implement this actions.
#if 0
						so_5::send< messages::quantity< std::size_t > >(
								mbox,
								m_base_prefix,
								suffix_disp_agent_count(),
								m_agent_count.load( std::memory_order_acquire ) );

						so_5::send< messages::quantity< std::size_t > >(
								mbox,
								m_base_prefix,
								suffix_disp_thread_count(),
								1u );
#endif
						so_5::send< stats::messages::quantity< std::size_t > >(
								mbox,
								m_work_thread_prefix,
								stats::suffix_work_thread_queue_size(),
								m_work_thread.demands_count() );
					}

				void
				set_data_sources_name_base(
					const std::string & name_base,
					const void * disp_this_pointer )
					{
						using namespace so_5::disp::reuse;

						m_base_prefix = make_disp_prefix(
								"ot", // ot -- one_thread
								name_base,
								disp_this_pointer );

						m_work_thread_prefix = make_disp_working_thread_prefix(
								m_base_prefix,
								0 );
					}

				void
				start(
					so_5::rt::environment_t & env )
					{
						env.stats_repository().add( *this );
						m_env = &env;
					}

				void
				stop()
					{
						m_env->stats_repository().remove( *this );
						m_env = nullptr;
					}
			};

		//! Working thread for the dispatcher.
		work_thread::work_thread_t m_work_thread;

		/*!
		 * \since v.5.5.4
		 * \brief Data source for run-time monitoring.
		 */
		data_source_t m_data_source;
	};

//
// disp_binder_t
//

//! Agent dispatcher binder.
class disp_binder_t : public so_5::rt::disp_binder_t
{
	public:
		explicit disp_binder_t(
			const std::string & disp_name )
			:	m_disp_name( disp_name )
		{}

		virtual so_5::rt::disp_binding_activator_t
		bind_agent(
			so_5::rt::environment_t & env,
			so_5::rt::agent_ref_t agent_ref ) override
		{
			if( m_disp_name.empty() )
				return make_agent_binding(
						&( env.query_default_dispatcher() ),
						std::move( agent_ref ) );
			else
				return make_agent_binding( 
					env.query_named_dispatcher( m_disp_name ).get(),
					std::move( agent_ref ) );
		}

		virtual void
		unbind_agent(
			so_5::rt::environment_t & /*env*/,
			so_5::rt::agent_ref_t /*agent_ref*/ ) override
		{
		}

	private:
		//! Name of the dispatcher to be bound to.
		/*!
		 * Empty name means usage of default dispatcher.
		 */
		const std::string m_disp_name;

		/*!
		 * \since v.5.4.0
		 * \brief Make binding to the dispatcher specified.
		 */
		so_5::rt::disp_binding_activator_t
		make_agent_binding(
			so_5::rt::dispatcher_t * disp,
			so_5::rt::agent_ref_t agent )
		{
			// If the dispatcher is found then the object should be bound to it.
			if( !disp )
				SO_5_THROW_EXCEPTION( rc_named_disp_not_found,
						"dispatcher with name \"" + m_disp_name + "\" not found" );

			// It should be exactly our dispatcher.
			dispatcher_t * d = dynamic_cast< dispatcher_t * >( disp );

			if( nullptr == d )
				SO_5_THROW_EXCEPTION( rc_disp_type_mismatch,
					"disp type mismatch for disp \"" + m_disp_name +
							"\", expected one_thread disp" );

			return [agent, d]() {
				agent->so_bind_to_dispatcher( *(d->get_agent_binding()) );
			};
		}
};

//
// private_dispatcher_binder_t
//

/*!
 * \since v.5.5.4
 * \brief A binder for the private %one_thread dispatcher.
 */
class private_dispatcher_binder_t : public so_5::rt::disp_binder_t
	{
	public:
		explicit private_dispatcher_binder_t(
			//! A handle for private dispatcher.
			//! It is necessary to manage lifetime of the dispatcher instance.
			private_dispatcher_handle_t handle,
			//! A dispatcher instance to work with.
			dispatcher_t & instance )
			:	m_handle( std::move( handle ) )
			,	m_instance( instance )
			{}

		virtual so_5::rt::disp_binding_activator_t
		bind_agent(
			so_5::rt::environment_t & /* env */,
			so_5::rt::agent_ref_t agent ) override
			{
				return [agent, this]() {
					agent->so_bind_to_dispatcher(
							*(m_instance.get_agent_binding()) );
				};
			}

		virtual void
		unbind_agent(
			so_5::rt::environment_t & /*env*/,
			so_5::rt::agent_ref_t /*agent_ref*/ ) override
			{}

	private:
		//! A handle for private dispatcher.
		/*!
		 * It is necessary to manage lifetime of the dispatcher instance.
		 */
		private_dispatcher_handle_t m_handle;
		//! A dispatcher instance to work with.
		dispatcher_t & m_instance;
	};

//
// real_private_dispatcher_t
//
/*!
 * \since v.5.5.4
 * \brief A real implementation of private_dispatcher interface.
 */
class real_private_dispatcher_t : public private_dispatcher_t
	{
	public :
		/*!
		 * Constructor creates a dispatcher instance and launces it.
		 */
		real_private_dispatcher_t(
			//! SObjectizer Environment to work in.
			so_5::rt::environment_t & env,
			//! Value for creating names of data sources for
			//! run-time monitoring.
			const std::string & data_sources_name_base )
			:	m_disp( new dispatcher_t() )
			{
				m_disp->set_data_sources_name_base( data_sources_name_base );
				m_disp->start( env );
			}

		/*!
		 * Destructors shuts an instance down and waits for it.
		 */
		~real_private_dispatcher_t()
			{
				m_disp->shutdown();
				m_disp->wait();
			}

		virtual so_5::rt::disp_binder_unique_ptr_t
		binder() override
			{
				return so_5::rt::disp_binder_unique_ptr_t(
						new private_dispatcher_binder_t(
								private_dispatcher_handle_t( this ),
								*m_disp ) );
			}

	private :
		std::unique_ptr< dispatcher_t > m_disp;
	};

} /* namespace impl */

//
// private_dispatcher_t
//
private_dispatcher_t::~private_dispatcher_t()
	{}

//
// create_disp
//
SO_5_FUNC so_5::rt::dispatcher_unique_ptr_t
create_disp()
{
	return so_5::rt::dispatcher_unique_ptr_t( new impl::dispatcher_t );
}

//
// create_private_disp
//
SO_5_FUNC private_dispatcher_handle_t
create_private_disp(
	so_5::rt::environment_t & env,
	const std::string & data_sources_name_base )
{
	return private_dispatcher_handle_t(
			new impl::real_private_dispatcher_t(
					env, data_sources_name_base ) );
}

//
// create_disp_binder
//
SO_5_FUNC so_5::rt::disp_binder_unique_ptr_t
create_disp_binder( const std::string & disp_name )
{
	return so_5::rt::disp_binder_unique_ptr_t(
		new impl::disp_binder_t( disp_name ) );
}

} /* namespace one_thread */

} /* namespace disp */

namespace rt
{

SO_5_FUNC disp_binder_unique_ptr_t
create_default_disp_binder()
{
	// Dispatcher with empty name means default dispatcher.
	return so_5::disp::one_thread::create_disp_binder( std::string() );
}

} /* namespace rt */

} /* namespace so_5 */

