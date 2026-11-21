#include <so_5/all.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>

#if defined(__clang__) && (__clang_major__ >= 16)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif

using namespace std::chrono;

//
// Tools for working with command line arguments.
//
inline bool
is_arg(
	const char * cmd_line_arg,
	const char * v1,
	const char * v2 )
	{
		return (0 == std::strcmp( cmd_line_arg, v1 ) ||
				0 == std::strcmp( cmd_line_arg, v2 ));
	}

template< class T >
void
arg_to_value( T & receiver,
	const char * cmd_line_arg_value,
	const char * name,
	const char * description )
	{
		std::stringstream ss;
		ss << cmd_line_arg_value;
		ss.seekg(0);

		T r;
		ss >> r;

		if( !ss || !ss.eof() )
			throw std::runtime_error(
					std::string( "unable to parse value for argument '" ) + name +
					"' (" + description + "): " +
					cmd_line_arg_value );

		receiver = r;
	}

template< class T >
void
mandatory_arg_to_value(
	T & receiver,
	char ** arg, char ** last_arg,
	const char * name,
	const char * description )
	{
		if( arg == last_arg )
			throw std::runtime_error(
					std::string( "argument '" ) + name + "' requires value (" +
					description + ")" );

		return arg_to_value( receiver, *arg, name, description );
	}

//
// Demo
//
enum class queue_lock_type_t
	{
		combined,
		simple
	};

struct cfg_t
	{
		unsigned int m_ring_size = 50000;
		unsigned int m_rounds = 1000;

		bool m_use_normal_mbox = false;
		bool m_use_signals = false;
		bool m_use_redirects = false;

		queue_lock_type_t m_queue_lock_type = queue_lock_type_t::combined;
	};

cfg_t
try_parse_cmdline(
	int argc,
	char ** argv )
{
	cfg_t tmp_cfg;

	for( char ** current = &argv[ 1 ], **last_arg = argv + argc;
			current != last_arg;
			++current )
		{
			if( is_arg( *current, "-h", "--help" ) )
				{
					std::cout << "usage:\n"
							"agent_ring <options>\n"
							"\noptions:\n"
							"-s, --ring-size      size of agent's ring\n"
							"-r, --rounds         count of full rounds around the ring\n"
							"-L, --queue-lock     type of queue lock to be used:\n"
							"                     combined, simple\n"
							"-m, --normal-mbox    use normal direct mbox for messages\n"
							"-S, --signal         your_turn as signal\n"
							"-R, --redirects      use redirects for your_turn messages\n"
							"-h, --help           show this help"
							<< std::endl;
					std::exit( 1 );
				}
			else if( is_arg( *current, "-s", "--ring-size" ) )
				mandatory_arg_to_value(
						tmp_cfg.m_ring_size, ++current, last_arg,
						"-s", "size of agent's ring" );
			else if( is_arg( *current, "-r", "--rounds" ) )
				mandatory_arg_to_value(
						tmp_cfg.m_rounds, ++current, last_arg,
						"-r", "count of full rounds around the ring" );
			else if( is_arg( *current, "-m", "--normal-mbox" ) )
				tmp_cfg.m_use_normal_mbox = true;
			else if( is_arg( *current, "-S", "--signal" ) )
				tmp_cfg.m_use_signals = true;
			else if( is_arg( *current, "-R", "--redirects" ) )
				tmp_cfg.m_use_redirects = true;
			else if( is_arg( *current, "-L", "--queue-lock" ) )
				{
					std::string name;
					mandatory_arg_to_value(
							name, ++current, last_arg,
							"-L", "queue lock type" );
					if( "combined" == name )
						tmp_cfg.m_queue_lock_type = queue_lock_type_t::combined;
					else if( "simple" == name )
						tmp_cfg.m_queue_lock_type = queue_lock_type_t::simple;
					else
						throw std::runtime_error( "unsupported queue lock type: " + name );
				}
			else
				throw std::runtime_error(
						std::string( "unknown argument: " ) + *current );
		}

	return tmp_cfg;
}

struct measure_result_t
	{
		steady_clock::time_point m_start_time;
		steady_clock::time_point m_finish_time;
	};

class a_ring_member_base_t : public so_5::agent_t
	{
	protected :
		so_5::mbox_t m_next_mbox;

	public :
		struct msg_start : public so_5::signal_t {};

		using so_5::agent_t::agent_t;

		void
		set_next_mbox( const so_5::mbox_t & mbox )
			{
				m_next_mbox = mbox;
			}
	};

template< typename Your_Turn_Base_Type >
class a_ring_member_t : public a_ring_member_base_t
	{
		static void
		direct_delivery_demand_handler(
			so_5::current_thread_id_t /*working_thread_id*/,
			so_5::execution_demand_t & d )
			{
				auto * self = static_cast<a_ring_member_t *>(d.m_receiver);
				self->on_message(
						d.m_msg_type,
						d.m_message_ref );
			}

		class special_direct_mbox_t final
			: public so_5::abstract_message_box_t
			{
				a_ring_member_t * m_target;

				const so_5::mbox_id_t m_id;

			public :
				special_direct_mbox_t(
					a_ring_member_t * target,
					so_5::mbox_id_t id )
					: m_target{ target }
					, m_id{ id }
					{}

				so_5::mbox_id_t
				id() const override { return m_id; }

				void
				subscribe_event_handler(
					const std::type_index & /*type_index*/,
					so_5::abstract_message_sink_t & /*subscriber*/ ) override
					{ /* Nothing to do */ }

				void
				unsubscribe_event_handler(
					const std::type_index & /*type_index*/,
					so_5::abstract_message_sink_t & /*subscriber*/ ) noexcept
					{ /* Nothing to do */ }

				std::string
				query_name() const override { return "<special_direct_mbox>"; }

				so_5::mbox_type_t
				type() const override
					{
						return so_5::mbox_type_t::multi_producer_single_consumer;
					}

				void
				do_deliver_message(
					so_5::message_delivery_mode_t /*delivery_mode*/,
					const std::type_index & msg_type,
					const so_5::message_ref_t & message,
					unsigned int /*redirection_deep*/ ) override
					{
						so_5::agent_t::call_push_demand(
								*m_target,
								nullptr,
								m_id,
								msg_type,
								message,
								&a_ring_member_t::direct_delivery_demand_handler
							);
					}

				void
				set_delivery_filter(
					const std::type_index & /*msg_type*/,
					const so_5::delivery_filter_t & /*filter*/,
					so_5::abstract_message_sink_t & /*subscriber*/ ) override
					{
						throw std::runtime_error{
								"set_delivery_filter is not supported"
							};
					}

				void
				drop_delivery_filter(
					const std::type_index & /*msg_type*/,
					so_5::abstract_message_sink_t & /*subscriber*/ ) noexcept override
					{ /* Nothing to do */ }

				so_5::environment_t &
				environment() const noexcept override
					{
						return m_target->so_environment();
					}
			};

		[[nodiscard]] static
		context_t
		prepare_context(
			context_t ctx,
			const cfg_t & cfg )
			{
				if( !cfg.m_use_normal_mbox )
					return ctx +
						custom_direct_mbox_factory(
							[]( so_5::partially_constructed_agent_ptr_t agent_ptr,
								so_5::mbox_t actual_mbox )
							{
								return so_5::mbox_t{
									std::make_unique<special_direct_mbox_t>(
										static_cast< a_ring_member_t * >( agent_ptr.ptr() ),
										actual_mbox->id()
									)
								};
							} );
				else
					return ctx;
			}

	public :
		struct msg_your_turn : public Your_Turn_Base_Type {};

		a_ring_member_t(
			context_t ctx,
			const cfg_t & cfg,
			measure_result_t & measure_result )
			: a_ring_member_base_t( prepare_context( ctx, cfg ) )
			, m_cfg( cfg )
			, m_measure_result( measure_result )
			{}

		void
		so_define_agent() override
			{
				so_subscribe_self()
					.event( &a_ring_member_t::evt_start )
					.event( &a_ring_member_t::evt_your_turn )
					;
			}

		void
		on_message(
			const std::type_index & msg_type,
			const so_5::message_ref_t & /*msg*/ )
			{
				if( msg_type == typeid(msg_start) ) [[unlikely]]
					{
						m_measure_result.m_start_time = steady_clock::now();

						so_5::send< msg_your_turn >( m_next_mbox );
					}
				else if( msg_type == typeid(msg_your_turn) ) [[likely]]
					{
						++m_rounds_passed;
						if( m_rounds_passed < m_cfg.m_rounds )
							so_5::send< msg_your_turn >( m_next_mbox );
						else
							{
								m_measure_result.m_finish_time = steady_clock::now();
								so_environment().stop();
							}
					}
			}

		void
		evt_start( mhood_t<msg_start> )
			{
				m_measure_result.m_start_time = steady_clock::now();

				so_5::send< msg_your_turn >( m_next_mbox );
			}

		void
		evt_your_turn( mhood_t<msg_your_turn> cmd )
			{
				++m_rounds_passed;
				if( m_rounds_passed < m_cfg.m_rounds )
					{
						if( m_cfg.m_use_redirects )
							so_5::send( m_next_mbox, cmd );
						else
							so_5::send< msg_your_turn >( m_next_mbox );
					}
				else
					{
						m_measure_result.m_finish_time = steady_clock::now();
						so_environment().stop();
					}
			}

	private :
		const cfg_t m_cfg;
		measure_result_t & m_measure_result;

		unsigned int m_rounds_passed = 0;
	};

const char *
queue_lock_type_name( queue_lock_type_t t )
	{
		if( queue_lock_type_t::combined == t )
			return "combined";
		else
			return "simple";
	}

void
show_cfg(
	const cfg_t & cfg )
	{
		std::cout << "Configuration:"
			<< "\n\t" "ring size: " << cfg.m_ring_size
			<< "\n\t" "rounds: " << cfg.m_rounds
			<< "\n\t" "queue_lock: " << queue_lock_type_name( cfg.m_queue_lock_type )
			<< "\n\t" "normal mbox: " << (cfg.m_use_normal_mbox ? "yes" : "no")
			<< "\n\t" "your_turn as signal: " << (cfg.m_use_signals ? "yes" : "no")
			<< "\n\t" "use redirects: " << (cfg.m_use_redirects ? "yes" : "no")
			;
		std::cout << std::endl;
	}

void
show_result(
	const cfg_t & cfg,
	const measure_result_t & result )
	{
		auto total_msec =
				duration_cast< milliseconds >( result.m_finish_time -
						result.m_start_time ).count();

		const unsigned long long total_msg_count =
			static_cast< unsigned long long >( cfg.m_ring_size ) * cfg.m_rounds;

		double price = static_cast< double >( total_msec ) /
				double(total_msg_count) / 1000.0;
		double throughtput = 1 / price;

		std::cout.precision( 10 );
		std::cout <<
			"total time: " << double(total_msec) / 1000.0 << 
			", messages sent: " << total_msg_count <<
			", price: " << price <<
			", throughtput: " << throughtput << std::endl;
	}

template<
	typename Disp_Params,
	typename Combined_Factory,
	typename Simple_Factory,
	typename Queue_Params_Tuner >
Disp_Params
make_disp_params(
	const cfg_t & cfg,
	Combined_Factory combined_factory,
	Simple_Factory simple_factory,
	Queue_Params_Tuner queue_params_tuner )
	{
		Disp_Params disp_params;
		using queue_params_t =
			typename std::decay< decltype(disp_params.queue_params()) >::type;

		disp_params.tune_queue_params( [&]( queue_params_t & p ) {
				if( queue_lock_type_t::simple == cfg.m_queue_lock_type )
					p.lock_factory( simple_factory() );
				else
					p.lock_factory( combined_factory() );
				queue_params_tuner( p );
			} );
		return disp_params;
	}

so_5::disp_binder_shptr_t
create_disp_binder(
	so_5::environment_t & env,
	const cfg_t & cfg )
	{
		using namespace so_5::disp::one_thread;
		auto disp_params = make_disp_params< disp_params_t >(
				cfg,
				[]{ return queue_traits::combined_lock_factory(); },
				[]{ return queue_traits::simple_lock_factory(); },
				[]( queue_traits::queue_params_t & ) {} );
		return make_dispatcher( env, "disp", disp_params ).binder();
	}

[[nodiscard]]
a_ring_member_base_t *
make_appropriate_agent(
	const cfg_t & cfg,
	measure_result_t & result,
	so_5::coop_t & coop )
	{
		if( cfg.m_use_signals )
			return coop.make_agent< a_ring_member_t< so_5::signal_t > >(
					cfg, result );
		else
			return coop.make_agent< a_ring_member_t< so_5::message_t > >(
					cfg, result );
	}

void
create_coop(
	const cfg_t & cfg,
	measure_result_t & result,
	so_5::environment_t & env )
	{
		so_5::mbox_t first_agent_mbox = env.introduce_coop(
			create_disp_binder( env, cfg ),
			[&]( so_5::coop_t & coop )
			{
				std::vector< a_ring_member_base_t * > agents;
				agents.reserve( cfg.m_ring_size );

				for( unsigned int i = 0; i != cfg.m_ring_size; ++i )
					{
						auto member = make_appropriate_agent( cfg, result, coop );
						agents.push_back( member );
					}

				for( unsigned int i = 0; i != cfg.m_ring_size; ++i )
					{
						agents[ i ]->set_next_mbox(
								agents[ (i + 1) % cfg.m_ring_size ]->so_direct_mbox() );
					}

				return agents[ 0 ]->so_direct_mbox();
			} );

		so_5::send< a_ring_member_base_t::msg_start >( first_agent_mbox );
	}

int
main( int argc, char ** argv )
{
	try
	{
		cfg_t cfg = try_parse_cmdline( argc, argv );
		show_cfg( cfg );

		measure_result_t result;

		so_5::launch(
				[&]( so_5::environment_t & env )
				{
					create_coop( cfg, result, env );
				} );

		show_result( cfg, result );

		return 0;
	}
	catch( const std::exception & x )
	{
		std::cerr << "*** Exception caught: " << x.what() << std::endl;
	}

	return 2;
}

