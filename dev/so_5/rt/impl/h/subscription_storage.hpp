/*
 * SObjectizer-5
 */

/*!
 * \since v.5.4.0
 * \file
 * \brief A storage for agent's subscriptions information.
 */

#pragma once

#include <map>
#include <unordered_map>

#include <so_5/h/types.hpp>

#include <so_5/rt/h/mbox.hpp>
#include <so_5/rt/h/state.hpp>
#include <so_5/rt/h/execution_demand.hpp>

namespace so_5
{

namespace rt
{

namespace impl
{

/*!
 * \since v.5.4.0
 * \brief Various details of subscription storage implementation.
 */
namespace subscription_details
{

//! Subscription key type.
struct key_t
{
	//! Unique ID of mbox.
	mbox_id_t m_mbox_id;
	//! Message type.
	std::type_index m_msg_type;
	//! State of agent.
	const state_t * m_state;

	//! Default constructor.
	inline key_t()
		:	m_state( nullptr )
		,	m_msg_type( typeid(void) )
		{}

	//! Constructor for the case when it is necessary to
	//! find all keys with (mbox_id, msg_type) prefix.
	inline key_t(
		mbox_id_t mbox_id,
		std::type_index msg_type )
		:	m_mbox_id( mbox_id )
		,	m_msg_type( msg_type )
		,	m_state( nullptr )
		{}

	//! Initializing constructor.
	inline key_t(
		mbox_id_t mbox_id,
		std::type_index msg_type,
		const state_t & state )
		:	m_mbox_id( mbox_id )
		,	m_msg_type( msg_type )
		,	m_state( &state )
		{}

	inline bool
	operator<( const key_t & o ) const
		{
			if( m_mbox_id < o.m_mbox_id )
				return true;
			else if( m_mbox_id == o.m_mbox_id )
				{
					if( m_msg_type < o.m_msg_type )
						return true;
					else if( m_msg_type == o.m_msg_type )
						return m_state < o.m_state;
				}

			return false;
		}

	inline bool
	operator==( const key_t & o ) const
		{
			return m_mbox_id == o.m_mbox_id &&
					m_msg_type == o.m_msg_type &&
					m_state == o.m_state;
		}

	inline bool
	is_same_mbox_msg_pair( const key_t & o ) const
		{
			return m_mbox_id == o.m_mbox_id &&
					m_msg_type == o.m_msg_type;
		}
};

} /* namespace subscription_details */

} /* namespace impl */

} /* namespace rt */

} /* namespace so_5 */

// Specialization of std::hash for subscription_details::key_t.
namespace std
{

template<>
struct hash< so_5::rt::impl::subscription_details::key_t >
	{
		typedef so_5::rt::impl::subscription_details::key_t argument_type;
		typedef std::size_t value_type;

		inline value_type
		operator()( const argument_type & a ) const
			{
				// This details has borrowed from documentation fo
				// boost::hash_combine function:
				// http://www.boost.org/doc/libs/1_46_1/doc/html/hash/reference.html#boost.hash_combine
				//
				const value_type h1 = hash< so_5::mbox_id_t >()( a.m_mbox_id );
				const value_type h2 = h1 ^
					(hash< type_index >()( a.m_msg_type ) + 0x9e3779b9 +
					 	(h1 << 6) + (h1 >> 2));

				return h2 ^ (hash< const so_5::rt::state_t * >()( a.m_state ) +
						0x9e3779b9 + (h2 << 6) + (h2 >> 2));
			}
	};

} /* namespace std */


namespace so_5
{

namespace rt
{

namespace impl
{

/*!
 * \since v.5.4.0
 * \brief A storage for agent's subscriptions information.
 *
 * The motivation for this class is the following:
 *
 * Initially an agent had only one subscription map where the key
 * was a compound key: (mbox_id, msg_type, state).
 *
 * This map was used in both operation: subscription creation/removing
 * and event handler searching. But this data structure was very inefficient
 * for event handler lookup. Especially when agent receive the same
 * message from same mbox in different states. It means that for most
 * lookup operations (mbox_id, msg_type) were the same.
 *
 * This class uses std::map and std::unordered_map for different purposes:
 * - std::map is used for subscription management (strictly ordered data
 *   structure makes subscription management a lot easier);
 * - std::unordered_map is used for event handler lookup operations
 *   (hash_table is more effiecient especially for the cases where the
 *   only difference in lookup keys is in state field).
 */
class subscription_storage_t
	{
	public :
		subscription_storage_t( agent_t * owner );
		~subscription_storage_t();

		void
		create_event_subscription(
			const mbox_ref_t & mbox_ref,
			std::type_index type_index,
			const state_t & target_state,
			const event_handler_method_t & method );

		void
		drop_subscription(
			const std::type_index & type_index,
			const mbox_ref_t & mbox_ref,
			const state_t & target_state );

		void
		drop_subscription_for_all_states(
			const std::type_index & type_index,
			const mbox_ref_t & mbox_ref );

		const event_handler_method_t *
		find_handler(
			mbox_id_t mbox_id,
			const std::type_index & msg_type,
			const state_t & current_state ) const;

	private :
		agent_t * m_owner;

		//! Type of subscription map.
		typedef std::map< subscription_details::key_t, mbox_ref_t > map_t;

		//! Map of subscriptions.
		/*!
		 * It is important to has right order of elements in this map:
		 * all subscription for the same (mbox,message) must be groupped
		 * together.
		 */
		map_t m_map;

		//! Type of event handlers hash table.
		typedef std::unordered_map<
						subscription_details::key_t,
						event_handler_method_t >
				hash_table_t;

		//! Hash table of event handlers.
		hash_table_t m_hash_table;

		void
		destroy_all_subscriptions();
	};

} /* namespace impl */

} /* namespace rt */

} /* namespace so_5 */

