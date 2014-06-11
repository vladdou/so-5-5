/*
 * A test for exception from service handler.
 */

#include <iostream>
#include <exception>
#include <sstream>

#include <so_5/rt/h/rt.hpp>
#include <so_5/api/h/api.hpp>
#include <so_5/h/types.hpp>

#include <so_5/disp/active_obj/h/pub.hpp>

#include "../a_time_sentinel.hpp"

struct msg_convert : public so_5::rt::message_t
	{
		int m_value;

		msg_convert( int value ) : m_value( value )
			{}
	};

class test_exception_ex_t : public std::runtime_error
	{
	public :
		test_exception_ex_t()
			:	std::runtime_error( "exception from convert service" )
			{}
	};

class a_convert_service_t
	:	public so_5::rt::agent_t
	{
	public :
		a_convert_service_t(
			so_5::rt::so_environment_t & env,
			const so_5::rt::mbox_ref_t & self_mbox )
			:	so_5::rt::agent_t( env )
			,	m_self_mbox( self_mbox )
			{}

		virtual void
		so_define_agent()
			{
				so_subscribe( m_self_mbox )
						.event( &a_convert_service_t::svc_convert );
			}

		std::string
		svc_convert( const so_5::rt::event_data_t< msg_convert > & evt )
			{
				if( 42 == evt->m_value )
					throw test_exception_ex_t();

				std::ostringstream s;
				s << evt->m_value;

				return s.str();
			}

	private :
		const so_5::rt::mbox_ref_t m_self_mbox;
	};

struct msg_shutdown : public so_5::rt::signal_t {};

class a_shutdowner_t
	:	public so_5::rt::agent_t
	{
	public :
		a_shutdowner_t(
			so_5::rt::so_environment_t & env,
			const so_5::rt::mbox_ref_t & self_mbox )
			:	so_5::rt::agent_t( env )
			,	m_self_mbox( self_mbox )
			{}

		virtual void
		so_define_agent()
			{
				so_subscribe( m_self_mbox )
						.event( &a_shutdowner_t::svc_shutdown );
			}

		void
		svc_shutdown( const so_5::rt::event_data_t< msg_shutdown > & evt )
			{
				so_environment().stop();
			}

	private :
		const so_5::rt::mbox_ref_t m_self_mbox;
	};

void
compare_and_abort_if_missmatch(
	const std::string & actual,
	const std::string & expected )
	{
		if( actual != expected )
			{
				std::cerr << "VALUE MISSMATCH: actual='"
						<< actual << "', expected='" << expected << "'"
						<< std::endl;
				std::abort();
			}
	}

class a_client_t
	:	public so_5::rt::agent_t
	{
	public :
		a_client_t(
			so_5::rt::so_environment_t & env,
			const so_5::rt::mbox_ref_t & svc_mbox )
			:	so_5::rt::agent_t( env )
			,	m_svc_mbox( svc_mbox )
			{}

		virtual so_5::rt::exception_reaction_t
		so_exception_reaction() const
			{
				return so_5::rt::abort_on_exception;
			}

		virtual void
		so_evt_start()
			{
				auto svc_proxy = so_5::rt::service< std::string >( m_svc_mbox );

				compare_and_abort_if_missmatch(
						svc_proxy.wait_forever().request( new msg_convert( 3 ) ),
						"3" );

				try
					{
						svc_proxy.wait_forever().request( new msg_convert( 42 ) );

						std::cerr << "SVC_HANDLER must throw exception!" << std::endl;

						std::abort();
					}
				catch( const test_exception_ex_t & x )
					{}

				so_5::rt::service< void >( m_svc_mbox )
						.wait_forever().request< msg_shutdown >();
			}

	private :
		const so_5::rt::mbox_ref_t m_svc_mbox;
	};

void
init(
	so_5::rt::so_environment_t & env )
	{
		auto coop = env.create_coop(
				so_5::rt::nonempty_name_t( "test_coop" ),
				so_5::disp::active_obj::create_disp_binder( "active_obj" ) );

		auto svc_mbox = env.create_local_mbox();

		coop->add_agent( new a_convert_service_t( env, svc_mbox ) );
		coop->add_agent( new a_shutdowner_t( env, svc_mbox ) );
		coop->add_agent( new a_client_t( env, svc_mbox ) );
		coop->add_agent( new a_time_sentinel_t( env ) );

		env.register_coop( std::move( coop ) );
	}

int
main( int, char ** )
	{
		try
			{
				so_5::api::run_so_environment(
					&init,
					std::move(
						so_5::rt::so_environment_params_t()
							.add_named_dispatcher(
								so_5::rt::nonempty_name_t( "active_obj" ),
								so_5::disp::active_obj::create_disp() ) ) );
			}
		catch( const std::exception & ex )
			{
				std::cerr << "Error: " << ex.what() << std::endl;
				return 1;
			}

		return 0;
	}
