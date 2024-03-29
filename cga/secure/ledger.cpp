#include <cga/node/common.hpp>
#include <cga/node/stats.hpp>
#include <cga/secure/blockstore.hpp>
#include <cga/secure/ledger.hpp>

namespace
{
/**
 * Roll back the visited block
 */
class rollback_visitor : public cga::block_visitor
{
public:
	rollback_visitor (cga::transaction const & transaction_a, cga::ledger & ledger_a, std::vector<cga::block_hash> & list_a) :
	transaction (transaction_a),
	ledger (ledger_a),
	list (list_a)
	{
	}
	virtual ~rollback_visitor () = default;
	void send_block (cga::send_block const & block_a) override
	{
		auto hash (block_a.hash ());
		cga::pending_info pending;
		cga::pending_key key (block_a.hashables.destination, hash);
		while (ledger.store.pending_get (transaction, key, pending))
		{
			ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.destination), list);
		}
		cga::account_info info;
		auto error (ledger.store.account_get (transaction, pending.source, info));
		assert (!error);
		ledger.store.pending_del (transaction, key);
		ledger.store.representation_add (transaction, ledger.representative (transaction, hash), pending.amount.number ());
		ledger.change_latest (transaction, pending.source, block_a.hashables.previous, info.rep_block, ledger.balance (transaction, block_a.hashables.previous), info.block_count - 1);
		ledger.store.block_del (transaction, hash);
		ledger.store.frontier_del (transaction, hash);
		ledger.store.frontier_put (transaction, block_a.hashables.previous, pending.source);
		ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
		ledger.stats.inc (cga::stat::type::rollback, cga::stat::detail::send);
	}
	void receive_block (cga::receive_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto representative (ledger.representative (transaction, block_a.hashables.previous));
		auto amount (ledger.amount (transaction, block_a.hashables.source));
		auto destination_account (ledger.account (transaction, hash));
		auto source_account (ledger.account (transaction, block_a.hashables.source));
		cga::account_info info;
		auto error (ledger.store.account_get (transaction, destination_account, info));
		assert (!error);
		ledger.store.representation_add (transaction, ledger.representative (transaction, hash), 0 - amount);
		ledger.change_latest (transaction, destination_account, block_a.hashables.previous, representative, ledger.balance (transaction, block_a.hashables.previous), info.block_count - 1);
		ledger.store.block_del (transaction, hash);
		ledger.store.pending_put (transaction, cga::pending_key (destination_account, block_a.hashables.source), { source_account, amount, cga::epoch::epoch_0 });
		ledger.store.frontier_del (transaction, hash);
		ledger.store.frontier_put (transaction, block_a.hashables.previous, destination_account);
		ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
		ledger.stats.inc (cga::stat::type::rollback, cga::stat::detail::receive);
	}
	void open_block (cga::open_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto amount (ledger.amount (transaction, block_a.hashables.source));
		auto destination_account (ledger.account (transaction, hash));
		auto source_account (ledger.account (transaction, block_a.hashables.source));
		ledger.store.representation_add (transaction, ledger.representative (transaction, hash), 0 - amount);
		ledger.change_latest (transaction, destination_account, 0, 0, 0, 0);
		ledger.store.block_del (transaction, hash);
		ledger.store.pending_put (transaction, cga::pending_key (destination_account, block_a.hashables.source), { source_account, amount, cga::epoch::epoch_0 });
		ledger.store.frontier_del (transaction, hash);
		ledger.stats.inc (cga::stat::type::rollback, cga::stat::detail::open);
	}
	void change_block (cga::change_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto representative (ledger.representative (transaction, block_a.hashables.previous));
		auto account (ledger.account (transaction, block_a.hashables.previous));
		cga::account_info info;
		auto error (ledger.store.account_get (transaction, account, info));
		assert (!error);
		auto balance (ledger.balance (transaction, block_a.hashables.previous));
		ledger.store.representation_add (transaction, representative, balance);
		ledger.store.representation_add (transaction, hash, 0 - balance);
		ledger.store.block_del (transaction, hash);
		ledger.change_latest (transaction, account, block_a.hashables.previous, representative, info.balance, info.block_count - 1);
		ledger.store.frontier_del (transaction, hash);
		ledger.store.frontier_put (transaction, block_a.hashables.previous, account);
		ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
		ledger.stats.inc (cga::stat::type::rollback, cga::stat::detail::change);
	}
	void state_block (cga::state_block const & block_a) override
	{
		auto hash (block_a.hash ());
		cga::block_hash representative (0);
		if (!block_a.hashables.previous.is_zero ())
		{
			representative = ledger.representative (transaction, block_a.hashables.previous);
		}
		auto balance (ledger.balance (transaction, block_a.hashables.previous));
		auto is_send (block_a.hashables.balance < balance);
		// Add in amount delta
		ledger.store.representation_add (transaction, hash, 0 - block_a.hashables.balance.number ());
		if (!representative.is_zero ())
		{
			// Move existing representation
			ledger.store.representation_add (transaction, representative, balance);
		}

		cga::account_info info;
		auto error (ledger.store.account_get (transaction, block_a.hashables.account, info));

		if (is_send)
		{
			cga::pending_key key (block_a.hashables.link, hash);
			while (!ledger.store.pending_exists (transaction, key))
			{
				ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.link), list);
			}
			ledger.store.pending_del (transaction, key);
			ledger.stats.inc (cga::stat::type::rollback, cga::stat::detail::send);
		}
		else if (!block_a.hashables.link.is_zero () && !ledger.is_epoch_link (block_a.hashables.link))
		{
			auto source_version (ledger.store.block_version (transaction, block_a.hashables.link));
			cga::pending_info pending_info (ledger.account (transaction, block_a.hashables.link), block_a.hashables.balance.number () - balance, source_version);
			ledger.store.pending_put (transaction, cga::pending_key (block_a.hashables.account, block_a.hashables.link), pending_info);
			ledger.stats.inc (cga::stat::type::rollback, cga::stat::detail::receive);
		}

		assert (!error);
		auto previous_version (ledger.store.block_version (transaction, block_a.hashables.previous));
		ledger.change_latest (transaction, block_a.hashables.account, block_a.hashables.previous, representative, balance, info.block_count - 1, false, previous_version);

		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		if (previous != nullptr)
		{
			ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
			if (previous->type () < cga::block_type::state)
			{
				ledger.store.frontier_put (transaction, block_a.hashables.previous, block_a.hashables.account);
			}
		}
		else
		{
			ledger.stats.inc (cga::stat::type::rollback, cga::stat::detail::open);
		}
		ledger.store.block_del (transaction, hash);
	}
	cga::transaction const & transaction;
	cga::ledger & ledger;
	std::vector<cga::block_hash> & list;
};

class ledger_processor : public cga::block_visitor
{
public:
	ledger_processor (cga::ledger &, cga::transaction const &, cga::signature_verification = cga::signature_verification::unknown);
	virtual ~ledger_processor () = default;
	void send_block (cga::send_block const &) override;
	void receive_block (cga::receive_block const &) override;
	void open_block (cga::open_block const &) override;
	void change_block (cga::change_block const &) override;
	void state_block (cga::state_block const &) override;
	void state_block_impl (cga::state_block const &);
	void epoch_block_impl (cga::state_block const &);
	cga::ledger & ledger;
	cga::transaction const & transaction;
	cga::signature_verification verification;
	cga::process_return result;
};

void ledger_processor::state_block (cga::state_block const & block_a)
{
	result.code = cga::process_result::progress;
	auto is_epoch_block (false);
	// Check if this is an epoch block
	if (!ledger.epoch_link.is_zero () && ledger.is_epoch_link (block_a.hashables.link))
	{
		cga::amount prev_balance (0);
		if (!block_a.hashables.previous.is_zero ())
		{
			result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? cga::process_result::progress : cga::process_result::gap_previous;
			if (result.code == cga::process_result::progress)
			{
				prev_balance = ledger.balance (transaction, block_a.hashables.previous);
			}
			else if (result.verified == cga::signature_verification::unknown)
			{
				// Check for possible regular state blocks with epoch link (send subtype)
				if (validate_message (block_a.hashables.account, block_a.hash (), block_a.signature))
				{
					// Is epoch block signed correctly
					if (validate_message (ledger.epoch_signer, block_a.hash (), block_a.signature))
					{
						result.verified = cga::signature_verification::invalid;
						result.code = cga::process_result::bad_signature;
					}
					else
					{
						result.verified = cga::signature_verification::valid_epoch;
					}
				}
				else
				{
					result.verified = cga::signature_verification::valid;
				}
			}
		}
		if (block_a.hashables.balance == prev_balance)
		{
			is_epoch_block = true;
		}
	}
	if (result.code == cga::process_result::progress)
	{
		if (is_epoch_block)
		{
			epoch_block_impl (block_a);
		}
		else
		{
			state_block_impl (block_a);
		}
	}
}

void ledger_processor::state_block_impl (cga::state_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? cga::process_result::old : cga::process_result::progress; // Have we seen this block before? (Unambiguous)
	if (result.code == cga::process_result::progress)
	{
		// Validate block if not verified outside of ledger
		if (result.verified != cga::signature_verification::valid)
		{
			result.code = validate_message (block_a.hashables.account, hash, block_a.signature) ? cga::process_result::bad_signature : cga::process_result::progress; // Is this block signed correctly (Unambiguous)
		}
		if (result.code == cga::process_result::progress)
		{
			assert (!validate_message (block_a.hashables.account, hash, block_a.signature));
			result.verified = cga::signature_verification::valid;
			result.code = block_a.hashables.account.is_zero () ? cga::process_result::opened_burn_account : cga::process_result::progress; // Is this for the burn account? (Unambiguous)
			if (result.code == cga::process_result::progress)
			{
				cga::epoch epoch (cga::epoch::epoch_0);
				cga::account_info info;
				result.amount = block_a.hashables.balance;
				auto is_send (false);
				auto account_error (ledger.store.account_get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					epoch = info.epoch;
					// Account already exists
					result.code = block_a.hashables.previous.is_zero () ? cga::process_result::fork : cga::process_result::progress; // Has this account already been opened? (Ambigious)
					if (result.code == cga::process_result::progress)
					{
						result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? cga::process_result::progress : cga::process_result::gap_previous; // Does the previous block exist in the ledger? (Unambigious)
						if (result.code == cga::process_result::progress)
						{
							is_send = block_a.hashables.balance < info.balance;
							result.amount = is_send ? (info.balance.number () - result.amount.number ()) : (result.amount.number () - info.balance.number ());
							result.code = block_a.hashables.previous == info.head ? cga::process_result::progress : cga::process_result::fork; // Is the previous block the account's head block? (Ambigious)
						}
					}
				}
				else
				{
					// Account does not yet exists
					result.code = block_a.previous ().is_zero () ? cga::process_result::progress : cga::process_result::gap_previous; // Does the first block in an account yield 0 for previous() ? (Unambigious)
					if (result.code == cga::process_result::progress)
					{
						result.code = !block_a.hashables.link.is_zero () ? cga::process_result::progress : cga::process_result::gap_source; // Is the first block receiving from a send ? (Unambigious)
					}
				}
				if (result.code == cga::process_result::progress)
				{
					if (!is_send)
					{
						if (!block_a.hashables.link.is_zero ())
						{
							result.code = ledger.store.source_exists (transaction, block_a.hashables.link) ? cga::process_result::progress : cga::process_result::gap_source; // Have we seen the source block already? (Harmless)
							if (result.code == cga::process_result::progress)
							{
								cga::pending_key key (block_a.hashables.account, block_a.hashables.link);
								cga::pending_info pending;
								result.code = ledger.store.pending_get (transaction, key, pending) ? cga::process_result::unreceivable : cga::process_result::progress; // Has this source already been received (Malformed)
								if (result.code == cga::process_result::progress)
								{
									result.code = result.amount == pending.amount ? cga::process_result::progress : cga::process_result::balance_mismatch;
									epoch = std::max (epoch, pending.epoch);
								}
							}
						}
						else
						{
							// If there's no link, the balance must remain the same, only the representative can change
							result.code = result.amount.is_zero () ? cga::process_result::progress : cga::process_result::balance_mismatch;
						}
					}
				}
				if (result.code == cga::process_result::progress)
				{
					ledger.stats.inc (cga::stat::type::ledger, cga::stat::detail::state_block);
					result.state_is_send = is_send;
					cga::block_sideband sideband (cga::block_type::state, block_a.hashables.account /* unused */, 0, 0 /* unused */, info.block_count + 1, cga::seconds_since_epoch ());
					ledger.store.block_put (transaction, hash, block_a, sideband, epoch);

					if (!info.rep_block.is_zero ())
					{
						// Move existing representation
						ledger.store.representation_add (transaction, info.rep_block, 0 - info.balance.number ());
					}
					// Add in amount delta
					ledger.store.representation_add (transaction, hash, block_a.hashables.balance.number ());

					if (is_send)
					{
						cga::pending_key key (block_a.hashables.link, hash);
						cga::pending_info info (block_a.hashables.account, result.amount.number (), epoch);
						ledger.store.pending_put (transaction, key, info);
					}
					else if (!block_a.hashables.link.is_zero ())
					{
						ledger.store.pending_del (transaction, cga::pending_key (block_a.hashables.account, block_a.hashables.link));
					}

					ledger.change_latest (transaction, block_a.hashables.account, hash, hash, block_a.hashables.balance, info.block_count + 1, true, epoch);
					if (!ledger.store.frontier_get (transaction, info.head).is_zero ())
					{
						ledger.store.frontier_del (transaction, info.head);
					}
					// Frontier table is unnecessary for state blocks and this also prevents old blocks from being inserted on top of state blocks
					result.account = block_a.hashables.account;
				}
			}
		}
	}
}

void ledger_processor::epoch_block_impl (cga::state_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? cga::process_result::old : cga::process_result::progress; // Have we seen this block before? (Unambiguous)
	if (result.code == cga::process_result::progress)
	{
		// Validate block if not verified outside of ledger
		if (result.verified != cga::signature_verification::valid_epoch)
		{
			result.code = validate_message (ledger.epoch_signer, hash, block_a.signature) ? cga::process_result::bad_signature : cga::process_result::progress; // Is this block signed correctly (Unambiguous)
		}
		if (result.code == cga::process_result::progress)
		{
			assert (!validate_message (ledger.epoch_signer, hash, block_a.signature));
			result.verified = cga::signature_verification::valid_epoch;
			result.code = block_a.hashables.account.is_zero () ? cga::process_result::opened_burn_account : cga::process_result::progress; // Is this for the burn account? (Unambiguous)
			if (result.code == cga::process_result::progress)
			{
				cga::account_info info;
				auto account_error (ledger.store.account_get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					// Account already exists
					result.code = block_a.hashables.previous.is_zero () ? cga::process_result::fork : cga::process_result::progress; // Has this account already been opened? (Ambigious)
					if (result.code == cga::process_result::progress)
					{
						result.code = block_a.hashables.previous == info.head ? cga::process_result::progress : cga::process_result::fork; // Is the previous block the account's head block? (Ambigious)
						if (result.code == cga::process_result::progress)
						{
							auto last_rep_block (ledger.store.block_get (transaction, info.rep_block));
							assert (last_rep_block != nullptr);
							result.code = block_a.hashables.representative == last_rep_block->representative () ? cga::process_result::progress : cga::process_result::representative_mismatch;
						}
					}
				}
				else
				{
					result.code = block_a.hashables.representative.is_zero () ? cga::process_result::progress : cga::process_result::representative_mismatch;
				}
				if (result.code == cga::process_result::progress)
				{
					result.code = info.epoch == cga::epoch::epoch_0 ? cga::process_result::progress : cga::process_result::block_position;
					if (result.code == cga::process_result::progress)
					{
						result.code = block_a.hashables.balance == info.balance ? cga::process_result::progress : cga::process_result::balance_mismatch;
						if (result.code == cga::process_result::progress)
						{
							ledger.stats.inc (cga::stat::type::ledger, cga::stat::detail::epoch_block);
							result.account = block_a.hashables.account;
							result.amount = 0;
							cga::block_sideband sideband (cga::block_type::state, block_a.hashables.account /* unused */, 0, 0 /* unused */, info.block_count + 1, cga::seconds_since_epoch ());
							ledger.store.block_put (transaction, hash, block_a, sideband, cga::epoch::epoch_1);
							ledger.change_latest (transaction, block_a.hashables.account, hash, hash, info.balance, info.block_count + 1, true, cga::epoch::epoch_1);
							if (!ledger.store.frontier_get (transaction, info.head).is_zero ())
							{
								ledger.store.frontier_del (transaction, info.head);
							}
						}
					}
				}
			}
		}
	}
}

void ledger_processor::change_block (cga::change_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? cga::process_result::old : cga::process_result::progress; // Have we seen this block before? (Harmless)
	if (result.code == cga::process_result::progress)
	{
		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? cga::process_result::progress : cga::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result.code == cga::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? cga::process_result::progress : cga::process_result::block_position;
			if (result.code == cga::process_result::progress)
			{
				auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? cga::process_result::fork : cga::process_result::progress;
				if (result.code == cga::process_result::progress)
				{
					cga::account_info info;
					auto latest_error (ledger.store.account_get (transaction, account, info));
					assert (!latest_error);
					assert (info.head == block_a.hashables.previous);
					// Validate block if not verified outside of ledger
					if (result.verified != cga::signature_verification::valid)
					{
						result.code = validate_message (account, hash, block_a.signature) ? cga::process_result::bad_signature : cga::process_result::progress; // Is this block signed correctly (Malformed)
					}
					if (result.code == cga::process_result::progress)
					{
						assert (!validate_message (account, hash, block_a.signature));
						result.verified = cga::signature_verification::valid;
						cga::block_sideband sideband (cga::block_type::change, account, 0, info.balance, info.block_count + 1, cga::seconds_since_epoch ());
						ledger.store.block_put (transaction, hash, block_a, sideband);
						auto balance (ledger.balance (transaction, block_a.hashables.previous));
						ledger.store.representation_add (transaction, hash, balance);
						ledger.store.representation_add (transaction, info.rep_block, 0 - balance);
						ledger.change_latest (transaction, account, hash, hash, info.balance, info.block_count + 1);
						ledger.store.frontier_del (transaction, block_a.hashables.previous);
						ledger.store.frontier_put (transaction, hash, account);
						result.account = account;
						result.amount = 0;
						ledger.stats.inc (cga::stat::type::ledger, cga::stat::detail::change);
					}
				}
			}
		}
	}
}

void ledger_processor::send_block (cga::send_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? cga::process_result::old : cga::process_result::progress; // Have we seen this block before? (Harmless)
	if (result.code == cga::process_result::progress)
	{
		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? cga::process_result::progress : cga::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result.code == cga::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? cga::process_result::progress : cga::process_result::block_position;
			if (result.code == cga::process_result::progress)
			{
				auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? cga::process_result::fork : cga::process_result::progress;
				if (result.code == cga::process_result::progress)
				{
					// Validate block if not verified outside of ledger
					if (result.verified != cga::signature_verification::valid)
					{
						result.code = validate_message (account, hash, block_a.signature) ? cga::process_result::bad_signature : cga::process_result::progress; // Is this block signed correctly (Malformed)
					}
					if (result.code == cga::process_result::progress)
					{
						assert (!validate_message (account, hash, block_a.signature));
						result.verified = cga::signature_verification::valid;
						cga::account_info info;
						auto latest_error (ledger.store.account_get (transaction, account, info));
						assert (!latest_error);
						assert (info.head == block_a.hashables.previous);
						result.code = info.balance.number () >= block_a.hashables.balance.number () ? cga::process_result::progress : cga::process_result::negative_spend; // Is this trying to spend a negative amount (Malicious)
						if (result.code == cga::process_result::progress)
						{
							auto amount (info.balance.number () - block_a.hashables.balance.number ());
							ledger.store.representation_add (transaction, info.rep_block, 0 - amount);
							cga::block_sideband sideband (cga::block_type::send, account, 0, block_a.hashables.balance /* unused */, info.block_count + 1, cga::seconds_since_epoch ());
							ledger.store.block_put (transaction, hash, block_a, sideband);
							ledger.change_latest (transaction, account, hash, info.rep_block, block_a.hashables.balance, info.block_count + 1);
							ledger.store.pending_put (transaction, cga::pending_key (block_a.hashables.destination, hash), { account, amount, cga::epoch::epoch_0 });
							ledger.store.frontier_del (transaction, block_a.hashables.previous);
							ledger.store.frontier_put (transaction, hash, account);
							result.account = account;
							result.amount = amount;
							result.pending_account = block_a.hashables.destination;
							ledger.stats.inc (cga::stat::type::ledger, cga::stat::detail::send);
						}
					}
				}
			}
		}
	}
}

void ledger_processor::receive_block (cga::receive_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? cga::process_result::old : cga::process_result::progress; // Have we seen this block already?  (Harmless)
	if (result.code == cga::process_result::progress)
	{
		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? cga::process_result::progress : cga::process_result::gap_previous;
		if (result.code == cga::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? cga::process_result::progress : cga::process_result::block_position;
			if (result.code == cga::process_result::progress)
			{
				auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? cga::process_result::gap_previous : cga::process_result::progress; //Have we seen the previous block? No entries for account at all (Harmless)
				if (result.code == cga::process_result::progress)
				{
					// Validate block if not verified outside of ledger
					if (result.verified != cga::signature_verification::valid)
					{
						result.code = validate_message (account, hash, block_a.signature) ? cga::process_result::bad_signature : cga::process_result::progress; // Is the signature valid (Malformed)
					}
					if (result.code == cga::process_result::progress)
					{
						assert (!validate_message (account, hash, block_a.signature));
						result.verified = cga::signature_verification::valid;
						result.code = ledger.store.source_exists (transaction, block_a.hashables.source) ? cga::process_result::progress : cga::process_result::gap_source; // Have we seen the source block already? (Harmless)
						if (result.code == cga::process_result::progress)
						{
							cga::account_info info;
							ledger.store.account_get (transaction, account, info);
							result.code = info.head == block_a.hashables.previous ? cga::process_result::progress : cga::process_result::gap_previous; // Block doesn't immediately follow latest block (Harmless)
							if (result.code == cga::process_result::progress)
							{
								cga::pending_key key (account, block_a.hashables.source);
								cga::pending_info pending;
								result.code = ledger.store.pending_get (transaction, key, pending) ? cga::process_result::unreceivable : cga::process_result::progress; // Has this source already been received (Malformed)
								if (result.code == cga::process_result::progress)
								{
									result.code = pending.epoch == cga::epoch::epoch_0 ? cga::process_result::progress : cga::process_result::unreceivable; // Are we receiving a state-only send? (Malformed)
									if (result.code == cga::process_result::progress)
									{
										auto new_balance (info.balance.number () + pending.amount.number ());
										cga::account_info source_info;
										auto error (ledger.store.account_get (transaction, pending.source, source_info));
										assert (!error);
										ledger.store.pending_del (transaction, key);
										cga::block_sideband sideband (cga::block_type::receive, account, 0, new_balance, info.block_count + 1, cga::seconds_since_epoch ());
										ledger.store.block_put (transaction, hash, block_a, sideband);
										ledger.change_latest (transaction, account, hash, info.rep_block, new_balance, info.block_count + 1);
										ledger.store.representation_add (transaction, info.rep_block, pending.amount.number ());
										ledger.store.frontier_del (transaction, block_a.hashables.previous);
										ledger.store.frontier_put (transaction, hash, account);
										result.account = account;
										result.amount = pending.amount;
										ledger.stats.inc (cga::stat::type::ledger, cga::stat::detail::receive);
									}
								}
							}
						}
					}
				}
				else
				{
					result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? cga::process_result::fork : cga::process_result::gap_previous; // If we have the block but it's not the latest we have a signed fork (Malicious)
				}
			}
		}
	}
}

void ledger_processor::open_block (cga::open_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? cga::process_result::old : cga::process_result::progress; // Have we seen this block already? (Harmless)
	if (result.code == cga::process_result::progress)
	{
		// Validate block if not verified outside of ledger
		if (result.verified != cga::signature_verification::valid)
		{
			result.code = validate_message (block_a.hashables.account, hash, block_a.signature) ? cga::process_result::bad_signature : cga::process_result::progress; // Is the signature valid (Malformed)
		}
		if (result.code == cga::process_result::progress)
		{
			assert (!validate_message (block_a.hashables.account, hash, block_a.signature));
			result.verified = cga::signature_verification::valid;
			result.code = ledger.store.source_exists (transaction, block_a.hashables.source) ? cga::process_result::progress : cga::process_result::gap_source; // Have we seen the source block? (Harmless)
			if (result.code == cga::process_result::progress)
			{
				cga::account_info info;
				result.code = ledger.store.account_get (transaction, block_a.hashables.account, info) ? cga::process_result::progress : cga::process_result::fork; // Has this account already been opened? (Malicious)
				if (result.code == cga::process_result::progress)
				{
					cga::pending_key key (block_a.hashables.account, block_a.hashables.source);
					cga::pending_info pending;
					result.code = ledger.store.pending_get (transaction, key, pending) ? cga::process_result::unreceivable : cga::process_result::progress; // Has this source already been received (Malformed)
					if (result.code == cga::process_result::progress)
					{
						result.code = block_a.hashables.account == cga::burn_account ? cga::process_result::opened_burn_account : cga::process_result::progress; // Is it burning 0 account? (Malicious)
						if (result.code == cga::process_result::progress)
						{
							result.code = pending.epoch == cga::epoch::epoch_0 ? cga::process_result::progress : cga::process_result::unreceivable; // Are we receiving a state-only send? (Malformed)
							if (result.code == cga::process_result::progress)
							{
								cga::account_info source_info;
								auto error (ledger.store.account_get (transaction, pending.source, source_info));
								assert (!error);
								ledger.store.pending_del (transaction, key);
								cga::block_sideband sideband (cga::block_type::open, block_a.hashables.account, 0, pending.amount, 1, cga::seconds_since_epoch ());
								ledger.store.block_put (transaction, hash, block_a, sideband);
								ledger.change_latest (transaction, block_a.hashables.account, hash, hash, pending.amount.number (), 1);
								ledger.store.representation_add (transaction, hash, pending.amount.number ());
								ledger.store.frontier_put (transaction, hash, block_a.hashables.account);
								result.account = block_a.hashables.account;
								result.amount = pending.amount;
								ledger.stats.inc (cga::stat::type::ledger, cga::stat::detail::open);
							}
						}
					}
				}
			}
		}
	}
}

ledger_processor::ledger_processor (cga::ledger & ledger_a, cga::transaction const & transaction_a, cga::signature_verification verification_a) :
ledger (ledger_a),
transaction (transaction_a),
verification (verification_a)
{
	result.verified = verification;
}
} // namespace

size_t cga::shared_ptr_block_hash::operator() (std::shared_ptr<cga::block> const & block_a) const
{
	auto hash (block_a->hash ());
	auto result (static_cast<size_t> (hash.qwords[0]));
	return result;
}

bool cga::shared_ptr_block_hash::operator() (std::shared_ptr<cga::block> const & lhs, std::shared_ptr<cga::block> const & rhs) const
{
	return lhs->hash () == rhs->hash ();
}

cga::ledger::ledger (cga::block_store & store_a, cga::stat & stat_a, cga::uint256_union const & epoch_link_a, cga::account const & epoch_signer_a) :
store (store_a),
stats (stat_a),
check_bootstrap_weights (true),
epoch_link (epoch_link_a),
epoch_signer (epoch_signer_a)
{
}

// Balance for account containing hash
cga::uint128_t cga::ledger::balance (cga::transaction const & transaction_a, cga::block_hash const & hash_a)
{
	return hash_a.is_zero () ? 0 : store.block_balance (transaction_a, hash_a);
}

// Balance for an account by account number
cga::uint128_t cga::ledger::account_balance (cga::transaction const & transaction_a, cga::account const & account_a)
{
	cga::uint128_t result (0);
	cga::account_info info;
	auto none (store.account_get (transaction_a, account_a, info));
	if (!none)
	{
		result = info.balance.number ();
	}
	return result;
}

cga::uint128_t cga::ledger::account_pending (cga::transaction const & transaction_a, cga::account const & account_a)
{
	cga::uint128_t result (0);
	cga::account end (account_a.number () + 1);
	for (auto i (store.pending_v0_begin (transaction_a, cga::pending_key (account_a, 0))), n (store.pending_v0_begin (transaction_a, cga::pending_key (end, 0))); i != n; ++i)
	{
		cga::pending_info info (i->second);
		result += info.amount.number ();
	}
	for (auto i (store.pending_v1_begin (transaction_a, cga::pending_key (account_a, 0))), n (store.pending_v1_begin (transaction_a, cga::pending_key (end, 0))); i != n; ++i)
	{
		cga::pending_info info (i->second);
		result += info.amount.number ();
	}
	return result;
}

cga::process_return cga::ledger::process (cga::transaction const & transaction_a, cga::block const & block_a, cga::signature_verification verification)
{
	ledger_processor processor (*this, transaction_a, verification);
	block_a.visit (processor);
	return processor.result;
}

cga::block_hash cga::ledger::representative (cga::transaction const & transaction_a, cga::block_hash const & hash_a)
{
	auto result (representative_calculated (transaction_a, hash_a));
	assert (result.is_zero () || store.block_exists (transaction_a, result));
	return result;
}

cga::block_hash cga::ledger::representative_calculated (cga::transaction const & transaction_a, cga::block_hash const & hash_a)
{
	representative_visitor visitor (transaction_a, store);
	visitor.compute (hash_a);
	return visitor.result;
}

bool cga::ledger::block_exists (cga::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	auto result (store.block_exists (transaction, hash_a));
	return result;
}

bool cga::ledger::block_exists (cga::block_type type, cga::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	auto result (store.block_exists (transaction, type, hash_a));
	return result;
}

std::string cga::ledger::block_text (char const * hash_a)
{
	return block_text (cga::block_hash (hash_a));
}

std::string cga::ledger::block_text (cga::block_hash const & hash_a)
{
	std::string result;
	auto transaction (store.tx_begin_read ());
	auto block (store.block_get (transaction, hash_a));
	if (block != nullptr)
	{
		block->serialize_json (result);
	}
	return result;
}

bool cga::ledger::is_send (cga::transaction const & transaction_a, cga::state_block const & block_a)
{
	bool result (false);
	cga::block_hash previous (block_a.hashables.previous);
	if (!previous.is_zero ())
	{
		if (block_a.hashables.balance < balance (transaction_a, previous))
		{
			result = true;
		}
	}
	return result;
}

cga::block_hash cga::ledger::block_destination (cga::transaction const & transaction_a, cga::block const & block_a)
{
	cga::block_hash result (0);
	cga::send_block const * send_block (dynamic_cast<cga::send_block const *> (&block_a));
	cga::state_block const * state_block (dynamic_cast<cga::state_block const *> (&block_a));
	if (send_block != nullptr)
	{
		result = send_block->hashables.destination;
	}
	else if (state_block != nullptr && is_send (transaction_a, *state_block))
	{
		result = state_block->hashables.link;
	}
	return result;
}

cga::block_hash cga::ledger::block_source (cga::transaction const & transaction_a, cga::block const & block_a)
{
	/*
	 * block_source() requires that the previous block of the block
	 * passed in exist in the database.  This is because it will try
	 * to check account balances to determine if it is a send block.
	 */
	assert (block_a.previous ().is_zero () || store.block_exists (transaction_a, block_a.previous ()));

	// If block_a.source () is nonzero, then we have our source.
	// However, universal blocks will always return zero.
	cga::block_hash result (block_a.source ());
	cga::state_block const * state_block (dynamic_cast<cga::state_block const *> (&block_a));
	if (state_block != nullptr && !is_send (transaction_a, *state_block))
	{
		result = state_block->hashables.link;
	}
	return result;
}

// Vote weight of an account
cga::uint128_t cga::ledger::weight (cga::transaction const & transaction_a, cga::account const & account_a)
{
	if (check_bootstrap_weights.load ())
	{
		auto blocks = store.block_count (transaction_a);
		if (blocks.sum () < bootstrap_weight_max_blocks)
		{
			auto weight = bootstrap_weights.find (account_a);
			if (weight != bootstrap_weights.end ())
			{
				return weight->second;
			}
		}
		else
		{
			check_bootstrap_weights = false;
		}
	}
	return store.representation_get (transaction_a, account_a);
}

// Rollback blocks until `block_a' doesn't exist
void cga::ledger::rollback (cga::transaction const & transaction_a, cga::block_hash const & block_a, std::vector<cga::block_hash> & list_a)
{
	assert (store.block_exists (transaction_a, block_a));
	auto account_l (account (transaction_a, block_a));
	rollback_visitor rollback (transaction_a, *this, list_a);
	cga::account_info info;
	while (store.block_exists (transaction_a, block_a))
	{
		auto latest_error (store.account_get (transaction_a, account_l, info));
		assert (!latest_error);
		auto block (store.block_get (transaction_a, info.head));
		list_a.push_back (info.head);
		block->visit (rollback);
	}
}

void cga::ledger::rollback (cga::transaction const & transaction_a, cga::block_hash const & block_a)
{
	std::vector<cga::block_hash> rollback_list;
	rollback (transaction_a, block_a, rollback_list);
}

// Return account containing hash
cga::account cga::ledger::account (cga::transaction const & transaction_a, cga::block_hash const & hash_a)
{
	return store.block_account (transaction_a, hash_a);
}

// Return amount decrease or increase for block
cga::uint128_t cga::ledger::amount (cga::transaction const & transaction_a, cga::block_hash const & hash_a)
{
	cga::uint128_t result;
	if (hash_a != cga::genesis_account)
	{
		auto block (store.block_get (transaction_a, hash_a));
		auto block_balance (balance (transaction_a, hash_a));
		auto previous_balance (balance (transaction_a, block->previous ()));
		result = block_balance > previous_balance ? block_balance - previous_balance : previous_balance - block_balance;
	}
	else
	{
		result = cga::genesis_amount;
	}
	return result;
}

// Return latest block for account
cga::block_hash cga::ledger::latest (cga::transaction const & transaction_a, cga::account const & account_a)
{
	cga::account_info info;
	auto latest_error (store.account_get (transaction_a, account_a, info));
	return latest_error ? 0 : info.head;
}

// Return latest root for account, account number of there are no blocks for this account.
cga::block_hash cga::ledger::latest_root (cga::transaction const & transaction_a, cga::account const & account_a)
{
	cga::account_info info;
	auto latest_error (store.account_get (transaction_a, account_a, info));
	cga::block_hash result;
	if (latest_error)
	{
		result = account_a;
	}
	else
	{
		result = info.head;
	}
	return result;
}

void cga::ledger::dump_account_chain (cga::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	auto hash (latest (transaction, account_a));
	while (!hash.is_zero ())
	{
		auto block (store.block_get (transaction, hash));
		assert (block != nullptr);
		std::cerr << hash.to_string () << std::endl;
		hash = block->previous ();
	}
}

class block_fit_visitor : public cga::block_visitor
{
public:
	block_fit_visitor (cga::ledger & ledger_a, cga::transaction const & transaction_a) :
	ledger (ledger_a),
	transaction (transaction_a),
	result (false)
	{
	}
	void send_block (cga::send_block const & block_a) override
	{
		result = ledger.store.block_exists (transaction, block_a.previous ());
	}
	void receive_block (cga::receive_block const & block_a) override
	{
		result = ledger.store.block_exists (transaction, block_a.previous ());
		result &= ledger.store.block_exists (transaction, block_a.source ());
	}
	void open_block (cga::open_block const & block_a) override
	{
		result = ledger.store.block_exists (transaction, block_a.source ());
	}
	void change_block (cga::change_block const & block_a) override
	{
		result = ledger.store.block_exists (transaction, block_a.previous ());
	}
	void state_block (cga::state_block const & block_a) override
	{
		result = block_a.previous ().is_zero () || ledger.store.block_exists (transaction, block_a.previous ());
		if (result && !ledger.is_send (transaction, block_a))
		{
			result &= ledger.store.block_exists (transaction, block_a.hashables.link) || block_a.hashables.link.is_zero () || ledger.is_epoch_link (block_a.hashables.link);
		}
	}
	cga::ledger & ledger;
	cga::transaction const & transaction;
	bool result;
};

bool cga::ledger::could_fit (cga::transaction const & transaction_a, cga::block const & block_a)
{
	block_fit_visitor visitor (*this, transaction_a);
	block_a.visit (visitor);
	return visitor.result;
}

bool cga::ledger::is_epoch_link (cga::uint256_union const & link_a)
{
	return link_a == epoch_link;
}

void cga::ledger::change_latest (cga::transaction const & transaction_a, cga::account const & account_a, cga::block_hash const & hash_a, cga::block_hash const & rep_block_a, cga::amount const & balance_a, uint64_t block_count_a, bool is_state, cga::epoch epoch_a)
{
	cga::account_info info;
	auto exists (!store.account_get (transaction_a, account_a, info));
	if (!exists)
	{
		assert (store.block_get (transaction_a, hash_a)->previous ().is_zero ());
		info.open_block = hash_a;
	}
	if (!hash_a.is_zero ())
	{
		info.head = hash_a;
		info.rep_block = rep_block_a;
		info.balance = balance_a;
		info.modified = cga::seconds_since_epoch ();
		info.block_count = block_count_a;
		if (exists && info.epoch != epoch_a)
		{
			// otherwise we'd end up with a duplicate
			store.account_del (transaction_a, account_a);
		}
		info.epoch = epoch_a;
		store.account_put (transaction_a, account_a, info);
	}
	else
	{
		store.account_del (transaction_a, account_a);
	}
}

std::shared_ptr<cga::block> cga::ledger::successor (cga::transaction const & transaction_a, cga::uint512_union const & root_a)
{
	cga::block_hash successor (0);
	if (root_a.uint256s[0].is_zero () && store.account_exists (transaction_a, root_a.uint256s[1]))
	{
		cga::account_info info;
		auto error (store.account_get (transaction_a, root_a.uint256s[1], info));
		assert (!error);
		successor = info.open_block;
	}
	else
	{
		successor = store.block_successor (transaction_a, root_a.uint256s[0]);
	}
	std::shared_ptr<cga::block> result;
	if (!successor.is_zero ())
	{
		result = store.block_get (transaction_a, successor);
	}
	assert (successor.is_zero () || result != nullptr);
	return result;
}

std::shared_ptr<cga::block> cga::ledger::forked_block (cga::transaction const & transaction_a, cga::block const & block_a)
{
	assert (!store.block_exists (transaction_a, block_a.type (), block_a.hash ()));
	auto root (block_a.root ());
	assert (store.block_exists (transaction_a, root) || store.account_exists (transaction_a, root));
	auto result (store.block_get (transaction_a, store.block_successor (transaction_a, root)));
	if (result == nullptr)
	{
		cga::account_info info;
		auto error (store.account_get (transaction_a, root, info));
		assert (!error);
		result = store.block_get (transaction_a, info.open_block);
		assert (result != nullptr);
	}
	return result;
}

namespace cga
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (ledger & ledger, const std::string & name)
{
	auto composite = std::make_unique<seq_con_info_composite> (name);
	auto count = ledger.bootstrap_weights.size ();
	auto sizeof_element = sizeof (decltype (ledger.bootstrap_weights)::value_type);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "bootstrap_weights", count, sizeof_element }));
	return composite;
}
}
