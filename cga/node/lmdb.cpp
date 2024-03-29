#include <cga/node/lmdb.hpp>

#include <cga/lib/utility.hpp>
#include <cga/node/common.hpp>
#include <cga/secure/versioning.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>

#include <queue>

cga::mdb_env::mdb_env (bool & error_a, boost::filesystem::path const & path_a, int max_dbs, size_t map_size_a)
{
	boost::system::error_code error_mkdir, error_chmod;
	if (path_a.has_parent_path ())
	{
		boost::filesystem::create_directories (path_a.parent_path (), error_mkdir);
		cga::set_secure_perm_directory (path_a.parent_path (), error_chmod);
		if (!error_mkdir)
		{
			auto status1 (mdb_env_create (&environment));
			release_assert (status1 == 0);
			auto status2 (mdb_env_set_maxdbs (environment, max_dbs));
			release_assert (status2 == 0);
			auto status3 (mdb_env_set_mapsize (environment, map_size_a));
			release_assert (status3 == 0);
			// It seems if there's ever more threads than mdb_env_set_maxreaders has read slots available, we get failures on transaction creation unless MDB_NOTLS is specified
			// This can happen if something like 256 io_threads are specified in the node config
			// MDB_NORDAHEAD will allow platforms that support it to load the DB in memory as needed.
			auto status4 (mdb_env_open (environment, path_a.string ().c_str (), MDB_NOSUBDIR | MDB_NOTLS | MDB_NORDAHEAD, 00600));
			release_assert (status4 == 0);
			error_a = status4 != 0;
		}
		else
		{
			error_a = true;
			environment = nullptr;
		}
	}
	else
	{
		error_a = true;
		environment = nullptr;
	}
}

cga::mdb_env::~mdb_env ()
{
	if (environment != nullptr)
	{
		mdb_env_close (environment);
	}
}

cga::mdb_env::operator MDB_env * () const
{
	return environment;
}

cga::transaction cga::mdb_env::tx_begin (bool write_a) const
{
	return { std::make_unique<cga::mdb_txn> (*this, write_a) };
}

MDB_txn * cga::mdb_env::tx (cga::transaction const & transaction_a) const
{
	auto result (boost::polymorphic_downcast<cga::mdb_txn *> (transaction_a.impl.get ()));
	release_assert (mdb_txn_env (result->handle) == environment);
	return *result;
}

cga::mdb_val::mdb_val (cga::epoch epoch_a) :
value ({ 0, nullptr }),
epoch (epoch_a)
{
}

cga::mdb_val::mdb_val (MDB_val const & value_a, cga::epoch epoch_a) :
value (value_a),
epoch (epoch_a)
{
}

cga::mdb_val::mdb_val (size_t size_a, void * data_a) :
value ({ size_a, data_a })
{
}

cga::mdb_val::mdb_val (cga::uint128_union const & val_a) :
mdb_val (sizeof (val_a), const_cast<cga::uint128_union *> (&val_a))
{
}

cga::mdb_val::mdb_val (cga::uint256_union const & val_a) :
mdb_val (sizeof (val_a), const_cast<cga::uint256_union *> (&val_a))
{
}

cga::mdb_val::mdb_val (cga::account_info const & val_a) :
mdb_val (val_a.db_size (), const_cast<cga::account_info *> (&val_a))
{
}

cga::mdb_val::mdb_val (cga::pending_info const & val_a) :
mdb_val (sizeof (val_a.source) + sizeof (val_a.amount), const_cast<cga::pending_info *> (&val_a))
{
}

cga::mdb_val::mdb_val (cga::pending_key const & val_a) :
mdb_val (sizeof (val_a), const_cast<cga::pending_key *> (&val_a))
{
}

cga::mdb_val::mdb_val (cga::unchecked_info const & val_a) :
buffer (std::make_shared<std::vector<uint8_t>> ())
{
	{
		cga::vectorstream stream (*buffer);
		val_a.serialize (stream);
	}
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}

cga::mdb_val::mdb_val (cga::block_info const & val_a) :
mdb_val (sizeof (val_a), const_cast<cga::block_info *> (&val_a))
{
}

cga::mdb_val::mdb_val (cga::endpoint_key const & val_a) :
mdb_val (sizeof (val_a), const_cast<cga::endpoint_key *> (&val_a))
{
}

cga::mdb_val::mdb_val (std::shared_ptr<cga::block> const & val_a) :
buffer (std::make_shared<std::vector<uint8_t>> ())
{
	{
		cga::vectorstream stream (*buffer);
		cga::serialize_block (stream, *val_a);
	}
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}

cga::mdb_val::mdb_val (uint64_t val_a) :
buffer (std::make_shared<std::vector<uint8_t>> ())
{
	{
		boost::endian::native_to_big_inplace (val_a);
		cga::vectorstream stream (*buffer);
		cga::write (stream, val_a);
	}
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}

void * cga::mdb_val::data () const
{
	return value.mv_data;
}

size_t cga::mdb_val::size () const
{
	return value.mv_size;
}

cga::mdb_val::operator cga::account_info () const
{
	cga::account_info result;
	result.epoch = epoch;
	assert (value.mv_size == result.db_size ());
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

cga::mdb_val::operator cga::block_info () const
{
	cga::block_info result;
	assert (value.mv_size == sizeof (result));
	static_assert (sizeof (cga::block_info::account) + sizeof (cga::block_info::balance) == sizeof (result), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
	return result;
}

cga::mdb_val::operator cga::pending_info () const
{
	cga::pending_info result;
	result.epoch = epoch;
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (cga::pending_info::source) + sizeof (cga::pending_info::amount), reinterpret_cast<uint8_t *> (&result));
	return result;
}

cga::mdb_val::operator cga::pending_key () const
{
	cga::pending_key result;
	assert (value.mv_size == sizeof (result));
	static_assert (sizeof (cga::pending_key::account) + sizeof (cga::pending_key::hash) == sizeof (result), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
	return result;
}

cga::mdb_val::operator cga::unchecked_info () const
{
	cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	cga::unchecked_info result;
	bool error (result.deserialize (stream));
	assert (!error);
	return result;
}

cga::mdb_val::operator cga::uint128_union () const
{
	cga::uint128_union result;
	assert (size () == sizeof (result));
	std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
	return result;
}

cga::mdb_val::operator cga::uint256_union () const
{
	cga::uint256_union result;
	assert (size () == sizeof (result));
	std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
	return result;
}

cga::mdb_val::operator std::array<char, 64> () const
{
	cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	std::array<char, 64> result;
	auto error = cga::try_read (stream, result);
	assert (!error);
	return result;
}

cga::mdb_val::operator cga::endpoint_key () const
{
	cga::endpoint_key result;
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
	return result;
}

cga::mdb_val::operator cga::no_value () const
{
	return no_value::dummy;
}

cga::mdb_val::operator std::shared_ptr<cga::block> () const
{
	cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	std::shared_ptr<cga::block> result (cga::deserialize_block (stream));
	return result;
}

cga::mdb_val::operator std::shared_ptr<cga::send_block> () const
{
	cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<cga::send_block> result (std::make_shared<cga::send_block> (error, stream));
	assert (!error);
	return result;
}

cga::mdb_val::operator std::shared_ptr<cga::receive_block> () const
{
	cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<cga::receive_block> result (std::make_shared<cga::receive_block> (error, stream));
	assert (!error);
	return result;
}

cga::mdb_val::operator std::shared_ptr<cga::open_block> () const
{
	cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<cga::open_block> result (std::make_shared<cga::open_block> (error, stream));
	assert (!error);
	return result;
}

cga::mdb_val::operator std::shared_ptr<cga::change_block> () const
{
	cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<cga::change_block> result (std::make_shared<cga::change_block> (error, stream));
	assert (!error);
	return result;
}

cga::mdb_val::operator std::shared_ptr<cga::state_block> () const
{
	cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<cga::state_block> result (std::make_shared<cga::state_block> (error, stream));
	assert (!error);
	return result;
}

cga::mdb_val::operator std::shared_ptr<cga::vote> () const
{
	cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<cga::vote> result (std::make_shared<cga::vote> (error, stream));
	assert (!error);
	return result;
}

cga::mdb_val::operator uint64_t () const
{
	uint64_t result;
	cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (cga::try_read (stream, result));
	assert (!error);
	boost::endian::big_to_native_inplace (result);
	return result;
}

cga::mdb_val::operator MDB_val * () const
{
	// Allow passing a temporary to a non-c++ function which doesn't have constness
	return const_cast<MDB_val *> (&value);
};

cga::mdb_val::operator MDB_val const & () const
{
	return value;
}

cga::mdb_txn::mdb_txn (cga::mdb_env const & environment_a, bool write_a)
{
	auto status (mdb_txn_begin (environment_a, nullptr, write_a ? 0 : MDB_RDONLY, &handle));
	release_assert (status == 0);
}

cga::mdb_txn::~mdb_txn ()
{
	auto status (mdb_txn_commit (handle));
	release_assert (status == 0);
}

cga::mdb_txn::operator MDB_txn * () const
{
	return handle;
}

namespace cga
{
/**
 * Fill in our predecessors
 */
class block_predecessor_set : public cga::block_visitor
{
public:
	block_predecessor_set (cga::transaction const & transaction_a, cga::mdb_store & store_a) :
	transaction (transaction_a),
	store (store_a)
	{
	}
	virtual ~block_predecessor_set () = default;
	void fill_value (cga::block const & block_a)
	{
		auto hash (block_a.hash ());
		cga::block_type type;
		auto value (store.block_raw_get (transaction, block_a.previous (), type));
		auto version (store.block_version (transaction, block_a.previous ()));
		assert (value.mv_size != 0);
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.mv_data), static_cast<uint8_t *> (value.mv_data) + value.mv_size);
		std::copy (hash.bytes.begin (), hash.bytes.end (), data.begin () + store.block_successor_offset (transaction, value, type));
		store.block_raw_put (transaction, store.block_database (type, version), block_a.previous (), cga::mdb_val (data.size (), data.data ()));
	}
	void send_block (cga::send_block const & block_a) override
	{
		fill_value (block_a);
	}
	void receive_block (cga::receive_block const & block_a) override
	{
		fill_value (block_a);
	}
	void open_block (cga::open_block const & block_a) override
	{
		// Open blocks don't have a predecessor
	}
	void change_block (cga::change_block const & block_a) override
	{
		fill_value (block_a);
	}
	void state_block (cga::state_block const & block_a) override
	{
		if (!block_a.previous ().is_zero ())
		{
			fill_value (block_a);
		}
	}
	cga::transaction const & transaction;
	cga::mdb_store & store;
};
}

template <typename T, typename U>
cga::mdb_iterator<T, U>::mdb_iterator (cga::transaction const & transaction_a, MDB_dbi db_a, cga::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
	auto status (mdb_cursor_open (tx (transaction_a), db_a, &cursor));
	release_assert (status == 0);
	auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_FIRST));
	release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
	if (status2 != MDB_NOTFOUND)
	{
		auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
		release_assert (status3 == 0 || status3 == MDB_NOTFOUND);
		if (current.first.size () != sizeof (T))
		{
			clear ();
		}
	}
	else
	{
		clear ();
	}
}

template <typename T, typename U>
cga::mdb_iterator<T, U>::mdb_iterator (std::nullptr_t, cga::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
}

template <typename T, typename U>
cga::mdb_iterator<T, U>::mdb_iterator (cga::transaction const & transaction_a, MDB_dbi db_a, MDB_val const & val_a, cga::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
	auto status (mdb_cursor_open (tx (transaction_a), db_a, &cursor));
	release_assert (status == 0);
	current.first = val_a;
	auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_SET_RANGE));
	release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
	if (status2 != MDB_NOTFOUND)
	{
		auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
		release_assert (status3 == 0 || status3 == MDB_NOTFOUND);
		if (current.first.size () != sizeof (T))
		{
			clear ();
		}
	}
	else
	{
		clear ();
	}
}

template <typename T, typename U>
cga::mdb_iterator<T, U>::mdb_iterator (cga::mdb_iterator<T, U> && other_a)
{
	cursor = other_a.cursor;
	other_a.cursor = nullptr;
	current = other_a.current;
}

template <typename T, typename U>
cga::mdb_iterator<T, U>::~mdb_iterator ()
{
	if (cursor != nullptr)
	{
		mdb_cursor_close (cursor);
	}
}

template <typename T, typename U>
cga::store_iterator_impl<T, U> & cga::mdb_iterator<T, U>::operator++ ()
{
	assert (cursor != nullptr);
	auto status (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_NEXT));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	if (status == MDB_NOTFOUND)
	{
		clear ();
	}
	if (current.first.size () != sizeof (T))
	{
		clear ();
	}
	return *this;
}

template <typename T, typename U>
cga::mdb_iterator<T, U> & cga::mdb_iterator<T, U>::operator= (cga::mdb_iterator<T, U> && other_a)
{
	if (cursor != nullptr)
	{
		mdb_cursor_close (cursor);
	}
	cursor = other_a.cursor;
	other_a.cursor = nullptr;
	current = other_a.current;
	other_a.clear ();
	return *this;
}

template <typename T, typename U>
std::pair<cga::mdb_val, cga::mdb_val> * cga::mdb_iterator<T, U>::operator-> ()
{
	return &current;
}

template <typename T, typename U>
bool cga::mdb_iterator<T, U>::operator== (cga::store_iterator_impl<T, U> const & base_a) const
{
	auto const other_a (boost::polymorphic_downcast<cga::mdb_iterator<T, U> const *> (&base_a));
	auto result (current.first.data () == other_a->current.first.data ());
	assert (!result || (current.first.size () == other_a->current.first.size ()));
	assert (!result || (current.second.data () == other_a->current.second.data ()));
	assert (!result || (current.second.size () == other_a->current.second.size ()));
	return result;
}

template <typename T, typename U>
void cga::mdb_iterator<T, U>::clear ()
{
	current.first = cga::mdb_val (current.first.epoch);
	current.second = cga::mdb_val (current.second.epoch);
	assert (is_end_sentinal ());
}

template <typename T, typename U>
MDB_txn * cga::mdb_iterator<T, U>::tx (cga::transaction const & transaction_a) const
{
	auto result (boost::polymorphic_downcast<cga::mdb_txn *> (transaction_a.impl.get ()));
	return *result;
}

template <typename T, typename U>
bool cga::mdb_iterator<T, U>::is_end_sentinal () const
{
	return current.first.size () == 0;
}

template <typename T, typename U>
void cga::mdb_iterator<T, U>::fill (std::pair<T, U> & value_a) const
{
	if (current.first.size () != 0)
	{
		value_a.first = static_cast<T> (current.first);
	}
	else
	{
		value_a.first = T ();
	}
	if (current.second.size () != 0)
	{
		value_a.second = static_cast<U> (current.second);
	}
	else
	{
		value_a.second = U ();
	}
}

template <typename T, typename U>
std::pair<cga::mdb_val, cga::mdb_val> * cga::mdb_merge_iterator<T, U>::operator-> ()
{
	return least_iterator ().operator-> ();
}

template <typename T, typename U>
cga::mdb_merge_iterator<T, U>::mdb_merge_iterator (cga::transaction const & transaction_a, MDB_dbi db1_a, MDB_dbi db2_a) :
impl1 (std::make_unique<cga::mdb_iterator<T, U>> (transaction_a, db1_a, cga::epoch::epoch_0)),
impl2 (std::make_unique<cga::mdb_iterator<T, U>> (transaction_a, db2_a, cga::epoch::epoch_1))
{
}

template <typename T, typename U>
cga::mdb_merge_iterator<T, U>::mdb_merge_iterator (std::nullptr_t) :
impl1 (std::make_unique<cga::mdb_iterator<T, U>> (nullptr, cga::epoch::epoch_0)),
impl2 (std::make_unique<cga::mdb_iterator<T, U>> (nullptr, cga::epoch::epoch_1))
{
}

template <typename T, typename U>
cga::mdb_merge_iterator<T, U>::mdb_merge_iterator (cga::transaction const & transaction_a, MDB_dbi db1_a, MDB_dbi db2_a, MDB_val const & val_a) :
impl1 (std::make_unique<cga::mdb_iterator<T, U>> (transaction_a, db1_a, val_a, cga::epoch::epoch_0)),
impl2 (std::make_unique<cga::mdb_iterator<T, U>> (transaction_a, db2_a, val_a, cga::epoch::epoch_1))
{
}

template <typename T, typename U>
cga::mdb_merge_iterator<T, U>::mdb_merge_iterator (cga::mdb_merge_iterator<T, U> && other_a)
{
	impl1 = std::move (other_a.impl1);
	impl2 = std::move (other_a.impl2);
}

template <typename T, typename U>
cga::mdb_merge_iterator<T, U>::~mdb_merge_iterator ()
{
}

template <typename T, typename U>
cga::store_iterator_impl<T, U> & cga::mdb_merge_iterator<T, U>::operator++ ()
{
	++least_iterator ();
	return *this;
}

template <typename T, typename U>
bool cga::mdb_merge_iterator<T, U>::is_end_sentinal () const
{
	return least_iterator ().is_end_sentinal ();
}

template <typename T, typename U>
void cga::mdb_merge_iterator<T, U>::fill (std::pair<T, U> & value_a) const
{
	auto & current (least_iterator ());
	if (current->first.size () != 0)
	{
		value_a.first = static_cast<T> (current->first);
	}
	else
	{
		value_a.first = T ();
	}
	if (current->second.size () != 0)
	{
		value_a.second = static_cast<U> (current->second);
	}
	else
	{
		value_a.second = U ();
	}
}

template <typename T, typename U>
bool cga::mdb_merge_iterator<T, U>::operator== (cga::store_iterator_impl<T, U> const & base_a) const
{
	assert ((dynamic_cast<cga::mdb_merge_iterator<T, U> const *> (&base_a) != nullptr) && "Incompatible iterator comparison");
	auto & other (static_cast<cga::mdb_merge_iterator<T, U> const &> (base_a));
	return *impl1 == *other.impl1 && *impl2 == *other.impl2;
}

template <typename T, typename U>
cga::mdb_iterator<T, U> & cga::mdb_merge_iterator<T, U>::least_iterator () const
{
	cga::mdb_iterator<T, U> * result;
	if (impl1->is_end_sentinal ())
	{
		result = impl2.get ();
	}
	else if (impl2->is_end_sentinal ())
	{
		result = impl1.get ();
	}
	else
	{
		auto key_cmp (mdb_cmp (mdb_cursor_txn (impl1->cursor), mdb_cursor_dbi (impl1->cursor), impl1->current.first, impl2->current.first));

		if (key_cmp < 0)
		{
			result = impl1.get ();
		}
		else if (key_cmp > 0)
		{
			result = impl2.get ();
		}
		else
		{
			auto val_cmp (mdb_cmp (mdb_cursor_txn (impl1->cursor), mdb_cursor_dbi (impl1->cursor), impl1->current.second, impl2->current.second));
			result = val_cmp < 0 ? impl1.get () : impl2.get ();
		}
	}
	return *result;
}

cga::wallet_value::wallet_value (cga::mdb_val const & val_a)
{
	assert (val_a.size () == sizeof (*this));
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), key.chars.begin ());
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key) + sizeof (work), reinterpret_cast<char *> (&work));
}

cga::wallet_value::wallet_value (cga::uint256_union const & key_a, uint64_t work_a) :
key (key_a),
work (work_a)
{
}

cga::mdb_val cga::wallet_value::val () const
{
	static_assert (sizeof (*this) == sizeof (key) + sizeof (work), "Class not packed");
	return cga::mdb_val (sizeof (*this), const_cast<cga::wallet_value *> (this));
}

template class cga::mdb_iterator<cga::pending_key, cga::pending_info>;
template class cga::mdb_iterator<cga::uint256_union, cga::block_info>;
template class cga::mdb_iterator<cga::uint256_union, cga::uint128_union>;
template class cga::mdb_iterator<cga::uint256_union, cga::uint256_union>;
template class cga::mdb_iterator<cga::uint256_union, std::shared_ptr<cga::block>>;
template class cga::mdb_iterator<cga::uint256_union, std::shared_ptr<cga::vote>>;
template class cga::mdb_iterator<cga::uint256_union, cga::wallet_value>;
template class cga::mdb_iterator<std::array<char, 64>, cga::no_value>;

cga::store_iterator<cga::account, cga::uint128_union> cga::mdb_store::representation_begin (cga::transaction const & transaction_a)
{
	cga::store_iterator<cga::account, cga::uint128_union> result (std::make_unique<cga::mdb_iterator<cga::account, cga::uint128_union>> (transaction_a, representation));
	return result;
}

cga::store_iterator<cga::account, cga::uint128_union> cga::mdb_store::representation_end ()
{
	cga::store_iterator<cga::account, cga::uint128_union> result (nullptr);
	return result;
}

cga::store_iterator<cga::unchecked_key, cga::unchecked_info> cga::mdb_store::unchecked_begin (cga::transaction const & transaction_a)
{
	cga::store_iterator<cga::unchecked_key, cga::unchecked_info> result (std::make_unique<cga::mdb_iterator<cga::unchecked_key, cga::unchecked_info>> (transaction_a, unchecked));
	return result;
}

cga::store_iterator<cga::unchecked_key, cga::unchecked_info> cga::mdb_store::unchecked_begin (cga::transaction const & transaction_a, cga::unchecked_key const & key_a)
{
	cga::store_iterator<cga::unchecked_key, cga::unchecked_info> result (std::make_unique<cga::mdb_iterator<cga::unchecked_key, cga::unchecked_info>> (transaction_a, unchecked, cga::mdb_val (key_a)));
	return result;
}

cga::store_iterator<cga::unchecked_key, cga::unchecked_info> cga::mdb_store::unchecked_end ()
{
	cga::store_iterator<cga::unchecked_key, cga::unchecked_info> result (nullptr);
	return result;
}

cga::store_iterator<cga::account, std::shared_ptr<cga::vote>> cga::mdb_store::vote_begin (cga::transaction const & transaction_a)
{
	return cga::store_iterator<cga::account, std::shared_ptr<cga::vote>> (std::make_unique<cga::mdb_iterator<cga::account, std::shared_ptr<cga::vote>>> (transaction_a, vote));
}

cga::store_iterator<cga::account, std::shared_ptr<cga::vote>> cga::mdb_store::vote_end ()
{
	return cga::store_iterator<cga::account, std::shared_ptr<cga::vote>> (nullptr);
}

cga::mdb_store::mdb_store (bool & error_a, cga::logging & logging_a, boost::filesystem::path const & path_a, int lmdb_max_dbs, bool drop_unchecked, size_t const batch_size) :
logging (logging_a),
env (error_a, path_a, lmdb_max_dbs)
{
	auto slow_upgrade (false);
	if (!error_a)
	{
		auto transaction (tx_begin_write ());
		error_a |= mdb_dbi_open (env.tx (transaction), "frontiers", MDB_CREATE, &frontiers) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "accounts", MDB_CREATE, &accounts_v0) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "accounts_v1", MDB_CREATE, &accounts_v1) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "send", MDB_CREATE, &send_blocks) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "receive", MDB_CREATE, &receive_blocks) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "open", MDB_CREATE, &open_blocks) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "change", MDB_CREATE, &change_blocks) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "state", MDB_CREATE, &state_blocks_v0) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "state_v1", MDB_CREATE, &state_blocks_v1) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "pending", MDB_CREATE, &pending_v0) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "pending_v1", MDB_CREATE, &pending_v1) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "representation", MDB_CREATE, &representation) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "unchecked", MDB_CREATE, &unchecked) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "vote", MDB_CREATE, &vote) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "online_weight", MDB_CREATE, &online_weight) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "meta", MDB_CREATE, &meta) != 0;
		error_a |= mdb_dbi_open (env.tx (transaction), "peers", MDB_CREATE, &peers) != 0;
		if (!full_sideband (transaction))
		{
			error_a |= mdb_dbi_open (env.tx (transaction), "blocks_info", MDB_CREATE, &blocks_info) != 0;
		}
		if (!error_a)
		{
			do_upgrades (transaction, slow_upgrade);
			if (drop_unchecked)
			{
				unchecked_clear (transaction);
			}
		}
	}
	if (slow_upgrade)
	{
		upgrades = std::thread ([this, batch_size]() {
			cga::thread_role::set (cga::thread_role::name::slow_db_upgrade);
			do_slow_upgrades (batch_size);
		});
	}
}

cga::mdb_store::~mdb_store ()
{
	stop ();
}

void cga::mdb_store::stop ()
{
	stopped = true;
	if (upgrades.joinable ())
	{
		upgrades.join ();
	}
}

cga::transaction cga::mdb_store::tx_begin_write ()
{
	return tx_begin (true);
}

cga::transaction cga::mdb_store::tx_begin_read ()
{
	return tx_begin (false);
}

cga::transaction cga::mdb_store::tx_begin (bool write_a)
{
	return env.tx_begin (write_a);
}

void cga::mdb_store::initialize (cga::transaction const & transaction_a, cga::genesis const & genesis_a)
{
	auto hash_l (genesis_a.hash ());
	assert (latest_v0_begin (transaction_a) == latest_v0_end ());
	assert (latest_v1_begin (transaction_a) == latest_v1_end ());
	cga::block_sideband sideband (cga::block_type::open, cga::genesis_account, 0, cga::genesis_amount, 1, cga::seconds_since_epoch ());
	block_put (transaction_a, hash_l, *genesis_a.open, sideband);
	account_put (transaction_a, genesis_account, { hash_l, genesis_a.open->hash (), genesis_a.open->hash (), std::numeric_limits<cga::uint128_t>::max (), cga::seconds_since_epoch (), 1, cga::epoch::epoch_0 });
	representation_put (transaction_a, genesis_account, std::numeric_limits<cga::uint128_t>::max ());
	frontier_put (transaction_a, hash_l, genesis_account);
}

void cga::mdb_store::version_put (cga::transaction const & transaction_a, int version_a)
{
	cga::uint256_union version_key (1);
	cga::uint256_union version_value (version_a);
	auto status (mdb_put (env.tx (transaction_a), meta, cga::mdb_val (version_key), cga::mdb_val (version_value), 0));
	release_assert (status == 0);
	if (blocks_info == 0 && !full_sideband (transaction_a))
	{
		auto status (mdb_dbi_open (env.tx (transaction_a), "blocks_info", MDB_CREATE, &blocks_info));
		release_assert (status == MDB_SUCCESS);
	}
	if (blocks_info != 0 && full_sideband (transaction_a))
	{
		auto status (mdb_drop (env.tx (transaction_a), blocks_info, 1));
		release_assert (status == MDB_SUCCESS);
		blocks_info = 0;
	}
}

int cga::mdb_store::version_get (cga::transaction const & transaction_a)
{
	cga::uint256_union version_key (1);
	cga::mdb_val data;
	auto error (mdb_get (env.tx (transaction_a), meta, cga::mdb_val (version_key), data));
	int result (1);
	if (error != MDB_NOTFOUND)
	{
		cga::uint256_union version_value (data);
		assert (version_value.qwords[2] == 0 && version_value.qwords[1] == 0 && version_value.qwords[0] == 0);
		result = version_value.number ().convert_to<int> ();
	}
	return result;
}

cga::raw_key cga::mdb_store::get_node_id (cga::transaction const & transaction_a)
{
	cga::uint256_union node_id_mdb_key (3);
	cga::raw_key node_id;
	cga::mdb_val value;
	auto error (mdb_get (env.tx (transaction_a), meta, cga::mdb_val (node_id_mdb_key), value));
	if (!error)
	{
		cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		error = cga::try_read (stream, node_id.data);
		assert (!error);
	}
	if (error)
	{
		cga::random_pool::generate_block (node_id.data.bytes.data (), node_id.data.bytes.size ());
		error = mdb_put (env.tx (transaction_a), meta, cga::mdb_val (node_id_mdb_key), cga::mdb_val (node_id.data), 0);
	}
	assert (!error);
	return node_id;
}

void cga::mdb_store::delete_node_id (cga::transaction const & transaction_a)
{
	cga::uint256_union node_id_mdb_key (3);
	auto error (mdb_del (env.tx (transaction_a), meta, cga::mdb_val (node_id_mdb_key), nullptr));
	assert (!error || error == MDB_NOTFOUND);
}

void cga::mdb_store::peer_put (cga::transaction const & transaction_a, cga::endpoint_key const & endpoint_a)
{
	cga::mdb_val zero (0);
	auto status (mdb_put (env.tx (transaction_a), peers, cga::mdb_val (endpoint_a), zero, 0));
	release_assert (status == 0);
}

void cga::mdb_store::peer_del (cga::transaction const & transaction_a, cga::endpoint_key const & endpoint_a)
{
	auto status (mdb_del (env.tx (transaction_a), peers, cga::mdb_val (endpoint_a), nullptr));
	release_assert (status == 0);
}

bool cga::mdb_store::peer_exists (cga::transaction const & transaction_a, cga::endpoint_key const & endpoint_a) const
{
	cga::mdb_val junk;
	auto status (mdb_get (env.tx (transaction_a), peers, cga::mdb_val (endpoint_a), junk));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	return (status == 0);
}

size_t cga::mdb_store::peer_count (cga::transaction const & transaction_a) const
{
	MDB_stat stats;
	auto status (mdb_stat (env.tx (transaction_a), peers, &stats));
	release_assert (status == 0);
	return stats.ms_entries;
}

void cga::mdb_store::peer_clear (cga::transaction const & transaction_a)
{
	auto status (mdb_drop (env.tx (transaction_a), peers, 0));
	release_assert (status == 0);
}

cga::store_iterator<cga::endpoint_key, cga::no_value> cga::mdb_store::peers_begin (cga::transaction const & transaction_a)
{
	cga::store_iterator<cga::endpoint_key, cga::no_value> result (std::make_unique<cga::mdb_iterator<cga::endpoint_key, cga::no_value>> (transaction_a, peers));
	return result;
}

cga::store_iterator<cga::endpoint_key, cga::no_value> cga::mdb_store::peers_end ()
{
	cga::store_iterator<cga::endpoint_key, cga::no_value> result (cga::store_iterator<cga::endpoint_key, cga::no_value> (nullptr));
	return result;
}

void cga::mdb_store::do_upgrades (cga::transaction const & transaction_a, bool & slow_upgrade)
{
	switch (version_get (transaction_a))
	{
		case 1:
			upgrade_v1_to_v2 (transaction_a);
		case 2:
			upgrade_v2_to_v3 (transaction_a);
		case 3:
			upgrade_v3_to_v4 (transaction_a);
		case 4:
			upgrade_v4_to_v5 (transaction_a);
		case 5:
			upgrade_v5_to_v6 (transaction_a);
		case 6:
			upgrade_v6_to_v7 (transaction_a);
		case 7:
			upgrade_v7_to_v8 (transaction_a);
		case 8:
			upgrade_v8_to_v9 (transaction_a);
		case 9:
			upgrade_v9_to_v10 (transaction_a);
		case 10:
			upgrade_v10_to_v11 (transaction_a);
		case 11:
			// Signal the start of sideband upgrade
			upgrade_v11_to_v12 (transaction_a);
			// [[fallthrough]];
		case 12:
			slow_upgrade = true;
			break;
		case 13:
			break;
		default:
			assert (false);
	}
}

void cga::mdb_store::upgrade_v1_to_v2 (cga::transaction const & transaction_a)
{
	version_put (transaction_a, 2);
	cga::account account (1);
	while (!account.is_zero ())
	{
		cga::mdb_iterator<cga::uint256_union, cga::account_info_v1> i (transaction_a, accounts_v0, cga::mdb_val (account));
		std::cerr << std::hex;
		if (i != cga::mdb_iterator<cga::uint256_union, cga::account_info_v1> (nullptr))
		{
			account = cga::uint256_union (i->first);
			cga::account_info_v1 v1 (i->second);
			cga::account_info_v5 v2;
			v2.balance = v1.balance;
			v2.head = v1.head;
			v2.modified = v1.modified;
			v2.rep_block = v1.rep_block;
			auto block (block_get (transaction_a, v1.head));
			while (!block->previous ().is_zero ())
			{
				block = block_get (transaction_a, block->previous ());
			}
			v2.open_block = block->hash ();
			auto status (mdb_put (env.tx (transaction_a), accounts_v0, cga::mdb_val (account), v2.val (), 0));
			release_assert (status == 0);
			account = account.number () + 1;
		}
		else
		{
			account.clear ();
		}
	}
}

void cga::mdb_store::upgrade_v2_to_v3 (cga::transaction const & transaction_a)
{
	version_put (transaction_a, 3);
	mdb_drop (env.tx (transaction_a), representation, 0);
	for (auto i (std::make_unique<cga::mdb_iterator<cga::account, cga::account_info_v5>> (transaction_a, accounts_v0)), n (std::make_unique<cga::mdb_iterator<cga::account, cga::account_info_v5>> (nullptr)); *i != *n; ++(*i))
	{
		cga::account account_l ((*i)->first);
		cga::account_info_v5 info ((*i)->second);
		representative_visitor visitor (transaction_a, *this);
		visitor.compute (info.head);
		assert (!visitor.result.is_zero ());
		info.rep_block = visitor.result;
		auto impl (boost::polymorphic_downcast<cga::mdb_iterator<cga::account, cga::account_info_v5> *> (i.get ()));
		mdb_cursor_put (impl->cursor, cga::mdb_val (account_l), info.val (), MDB_CURRENT);
		representation_add (transaction_a, visitor.result, info.balance.number ());
	}
}

void cga::mdb_store::upgrade_v3_to_v4 (cga::transaction const & transaction_a)
{
	version_put (transaction_a, 4);
	std::queue<std::pair<cga::pending_key, cga::pending_info>> items;
	for (auto i (cga::store_iterator<cga::block_hash, cga::pending_info_v3> (std::make_unique<cga::mdb_iterator<cga::block_hash, cga::pending_info_v3>> (transaction_a, pending_v0))), n (cga::store_iterator<cga::block_hash, cga::pending_info_v3> (nullptr)); i != n; ++i)
	{
		cga::block_hash hash (i->first);
		cga::pending_info_v3 info (i->second);
		items.push (std::make_pair (cga::pending_key (info.destination, hash), cga::pending_info (info.source, info.amount, cga::epoch::epoch_0)));
	}
	mdb_drop (env.tx (transaction_a), pending_v0, 0);
	while (!items.empty ())
	{
		pending_put (transaction_a, items.front ().first, items.front ().second);
		items.pop ();
	}
}

void cga::mdb_store::upgrade_v4_to_v5 (cga::transaction const & transaction_a)
{
	version_put (transaction_a, 5);
	for (auto i (cga::store_iterator<cga::account, cga::account_info_v5> (std::make_unique<cga::mdb_iterator<cga::account, cga::account_info_v5>> (transaction_a, accounts_v0))), n (cga::store_iterator<cga::account, cga::account_info_v5> (nullptr)); i != n; ++i)
	{
		cga::account_info_v5 info (i->second);
		cga::block_hash successor (0);
		auto block (block_get (transaction_a, info.head));
		while (block != nullptr)
		{
			auto hash (block->hash ());
			if (block_successor (transaction_a, hash).is_zero () && !successor.is_zero ())
			{
				std::vector<uint8_t> vector;
				{
					cga::vectorstream stream (vector);
					block->serialize (stream);
					cga::write (stream, successor.bytes);
				}
				block_raw_put (transaction_a, block_database (block->type (), cga::epoch::epoch_0), hash, { vector.size (), vector.data () });
				if (!block->previous ().is_zero ())
				{
					cga::block_type type;
					auto value (block_raw_get (transaction_a, block->previous (), type));
					auto version (block_version (transaction_a, block->previous ()));
					assert (value.mv_size != 0);
					std::vector<uint8_t> data (static_cast<uint8_t *> (value.mv_data), static_cast<uint8_t *> (value.mv_data) + value.mv_size);
					std::copy (hash.bytes.begin (), hash.bytes.end (), data.end () - cga::block_sideband::size (type));
					block_raw_put (transaction_a, block_database (type, version), block->previous (), cga::mdb_val (data.size (), data.data ()));
				}
			}
			successor = hash;
			block = block_get (transaction_a, block->previous ());
		}
	}
}

void cga::mdb_store::upgrade_v5_to_v6 (cga::transaction const & transaction_a)
{
	version_put (transaction_a, 6);
	std::deque<std::pair<cga::account, cga::account_info>> headers;
	for (auto i (cga::store_iterator<cga::account, cga::account_info_v5> (std::make_unique<cga::mdb_iterator<cga::account, cga::account_info_v5>> (transaction_a, accounts_v0))), n (cga::store_iterator<cga::account, cga::account_info_v5> (nullptr)); i != n; ++i)
	{
		cga::account account (i->first);
		cga::account_info_v5 info_old (i->second);
		uint64_t block_count (0);
		auto hash (info_old.head);
		while (!hash.is_zero ())
		{
			++block_count;
			auto block (block_get (transaction_a, hash));
			assert (block != nullptr);
			hash = block->previous ();
		}
		cga::account_info info (info_old.head, info_old.rep_block, info_old.open_block, info_old.balance, info_old.modified, block_count, cga::epoch::epoch_0);
		headers.push_back (std::make_pair (account, info));
	}
	for (auto i (headers.begin ()), n (headers.end ()); i != n; ++i)
	{
		account_put (transaction_a, i->first, i->second);
	}
}

void cga::mdb_store::upgrade_v6_to_v7 (cga::transaction const & transaction_a)
{
	version_put (transaction_a, 7);
	mdb_drop (env.tx (transaction_a), unchecked, 0);
}

void cga::mdb_store::upgrade_v7_to_v8 (cga::transaction const & transaction_a)
{
	version_put (transaction_a, 8);
	mdb_drop (env.tx (transaction_a), unchecked, 1);
	mdb_dbi_open (env.tx (transaction_a), "unchecked", MDB_CREATE | MDB_DUPSORT, &unchecked);
}

void cga::mdb_store::upgrade_v8_to_v9 (cga::transaction const & transaction_a)
{
	version_put (transaction_a, 9);
	MDB_dbi sequence;
	mdb_dbi_open (env.tx (transaction_a), "sequence", MDB_CREATE | MDB_DUPSORT, &sequence);
	cga::genesis genesis;
	std::shared_ptr<cga::block> block (std::move (genesis.open));
	cga::keypair junk;
	for (cga::mdb_iterator<cga::account, uint64_t> i (transaction_a, sequence), n (cga::mdb_iterator<cga::account, uint64_t> (nullptr)); i != n; ++i)
	{
		cga::bufferstream stream (reinterpret_cast<uint8_t const *> (i->second.data ()), i->second.size ());
		uint64_t sequence;
		auto error (cga::try_read (stream, sequence));
		// Create a dummy vote with the same sequence number for easy upgrading.  This won't have a valid signature.
		cga::vote dummy (cga::account (i->first), junk.prv, sequence, block);
		std::vector<uint8_t> vector;
		{
			cga::vectorstream stream (vector);
			dummy.serialize (stream);
		}
		auto status1 (mdb_put (env.tx (transaction_a), vote, cga::mdb_val (i->first), cga::mdb_val (vector.size (), vector.data ()), 0));
		release_assert (status1 == 0);
		assert (!error);
	}
	mdb_drop (env.tx (transaction_a), sequence, 1);
}

void cga::mdb_store::upgrade_v9_to_v10 (cga::transaction const & transaction_a)
{
}

void cga::mdb_store::upgrade_v10_to_v11 (cga::transaction const & transaction_a)
{
	version_put (transaction_a, 11);
	MDB_dbi unsynced;
	mdb_dbi_open (env.tx (transaction_a), "unsynced", MDB_CREATE | MDB_DUPSORT, &unsynced);
	mdb_drop (env.tx (transaction_a), unsynced, 1);
}

void cga::mdb_store::do_slow_upgrades (size_t const batch_size)
{
	int version;
	{
		cga::transaction transaction (tx_begin_read ());
		version = version_get (transaction);
	}
	switch (version)
	{
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
			break;
		case 12:
			upgrade_v12_to_v13 (batch_size);
			break;
		case 13:
			break;
		default:
			assert (false);
			break;
	}
}

void cga::mdb_store::upgrade_v11_to_v12 (cga::transaction const & transaction_a)
{
	version_put (transaction_a, 12);
	mdb_drop (env.tx (transaction_a), unchecked, 1);
	mdb_dbi_open (env.tx (transaction_a), "unchecked", MDB_CREATE, &unchecked);
	MDB_dbi checksum;
	mdb_dbi_open (env.tx (transaction_a), "checksum", MDB_CREATE, &checksum);
	mdb_drop (env.tx (transaction_a), checksum, 1);
}

void cga::mdb_store::upgrade_v12_to_v13 (size_t const batch_size)
{
	size_t cost (0);
	cga::account account (0);
	auto transaction (tx_begin_write ());
	auto const & not_an_account (cga::not_an_account ());
	while (!stopped && account != not_an_account)
	{
		cga::account first (0);
		cga::account_info second;
		{
			auto current (latest_begin (transaction, account));
			if (current != latest_end ())
			{
				first = current->first;
				second = current->second;
			}
		}
		if (!first.is_zero ())
		{
			auto hash (second.open_block);
			uint64_t height (1);
			cga::block_sideband sideband;
			while (!stopped && !hash.is_zero ())
			{
				if (cost >= batch_size)
				{
					BOOST_LOG (logging.log) << boost::str (boost::format ("Upgrading sideband information for account %1%... height %2%") % first.to_account ().substr (0, 24) % std::to_string (height));
					auto tx (boost::polymorphic_downcast<cga::mdb_txn *> (transaction.impl.get ()));
					auto status0 (mdb_txn_commit (*tx));
					release_assert (status0 == MDB_SUCCESS);
					std::this_thread::yield ();
					auto status1 (mdb_txn_begin (env, nullptr, 0, &tx->handle));
					release_assert (status1 == MDB_SUCCESS);
					cost = 0;
				}
				auto block (block_get (transaction, hash, &sideband));
				assert (block != nullptr);
				if (sideband.height == 0)
				{
					sideband.height = height;
					block_put (transaction, hash, *block, sideband, block_version (transaction, hash));
					cost += 16;
				}
				else
				{
					cost += 1;
				}
				hash = sideband.successor;
				++height;
			}
			account = first.number () + 1;
		}
		else
		{
			account = not_an_account;
		}
	}
	if (account == not_an_account)
	{
		BOOST_LOG (logging.log) << boost::str (boost::format ("Completed sideband upgrade"));
		version_put (transaction, 13);
	}
}

void cga::mdb_store::clear (MDB_dbi db_a)
{
	auto transaction (tx_begin_write ());
	auto status (mdb_drop (env.tx (transaction), db_a, 0));
	release_assert (status == 0);
}

cga::uint128_t cga::mdb_store::block_balance (cga::transaction const & transaction_a, cga::block_hash const & hash_a)
{
	cga::block_sideband sideband;
	auto block (block_get (transaction_a, hash_a, &sideband));
	cga::uint128_t result;
	switch (block->type ())
	{
		case cga::block_type::open:
		case cga::block_type::receive:
		case cga::block_type::change:
			result = sideband.balance.number ();
			break;
		case cga::block_type::send:
			result = boost::polymorphic_downcast<cga::send_block *> (block.get ())->hashables.balance.number ();
			break;
		case cga::block_type::state:
			result = boost::polymorphic_downcast<cga::state_block *> (block.get ())->hashables.balance.number ();
			break;
		case cga::block_type::invalid:
		case cga::block_type::not_a_block:
			release_assert (false);
			break;
	}
	return result;
}

cga::uint128_t cga::mdb_store::block_balance_computed (cga::transaction const & transaction_a, cga::block_hash const & hash_a)
{
	assert (!full_sideband (transaction_a));
	summation_visitor visitor (transaction_a, *this);
	return visitor.compute_balance (hash_a);
}

cga::epoch cga::mdb_store::block_version (cga::transaction const & transaction_a, cga::block_hash const & hash_a)
{
	cga::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), state_blocks_v1, cga::mdb_val (hash_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	return status == 0 ? cga::epoch::epoch_1 : cga::epoch::epoch_0;
}

void cga::mdb_store::representation_add (cga::transaction const & transaction_a, cga::block_hash const & source_a, cga::uint128_t const & amount_a)
{
	auto source_block (block_get (transaction_a, source_a));
	assert (source_block != nullptr);
	auto source_rep (source_block->representative ());
	auto source_previous (representation_get (transaction_a, source_rep));
	representation_put (transaction_a, source_rep, source_previous + amount_a);
}

MDB_dbi cga::mdb_store::block_database (cga::block_type type_a, cga::epoch epoch_a)
{
	if (type_a == cga::block_type::state)
	{
		assert (epoch_a == cga::epoch::epoch_0 || epoch_a == cga::epoch::epoch_1);
	}
	else
	{
		assert (epoch_a == cga::epoch::epoch_0);
	}
	MDB_dbi result;
	switch (type_a)
	{
		case cga::block_type::send:
			result = send_blocks;
			break;
		case cga::block_type::receive:
			result = receive_blocks;
			break;
		case cga::block_type::open:
			result = open_blocks;
			break;
		case cga::block_type::change:
			result = change_blocks;
			break;
		case cga::block_type::state:
			switch (epoch_a)
			{
				case cga::epoch::epoch_0:
					result = state_blocks_v0;
					break;
				case cga::epoch::epoch_1:
					result = state_blocks_v1;
					break;
				default:
					assert (false);
			}
			break;
		default:
			assert (false);
			break;
	}
	return result;
}

void cga::mdb_store::block_raw_put (cga::transaction const & transaction_a, MDB_dbi database_a, cga::block_hash const & hash_a, MDB_val value_a)
{
	auto status2 (mdb_put (env.tx (transaction_a), database_a, cga::mdb_val (hash_a), &value_a, 0));
	release_assert (status2 == 0);
}

void cga::mdb_store::block_put (cga::transaction const & transaction_a, cga::block_hash const & hash_a, cga::block const & block_a, cga::block_sideband const & sideband_a, cga::epoch epoch_a)
{
	assert (block_a.type () == sideband_a.type);
	assert (sideband_a.successor.is_zero () || block_exists (transaction_a, sideband_a.successor));
	std::vector<uint8_t> vector;
	{
		cga::vectorstream stream (vector);
		block_a.serialize (stream);
		sideband_a.serialize (stream);
	}
	block_raw_put (transaction_a, block_database (block_a.type (), epoch_a), hash_a, { vector.size (), vector.data () });
	cga::block_predecessor_set predecessor (transaction_a, *this);
	block_a.visit (predecessor);
	assert (block_a.previous ().is_zero () || block_successor (transaction_a, block_a.previous ()) == hash_a);
}

boost::optional<MDB_val> cga::mdb_store::block_raw_get_by_type (cga::transaction const & transaction_a, cga::block_hash const & hash_a, cga::block_type & type_a)
{
	cga::mdb_val value;
	auto status (MDB_NOTFOUND);
	switch (type_a)
	{
		case cga::block_type::send:
		{
			status = mdb_get (env.tx (transaction_a), send_blocks, cga::mdb_val (hash_a), value);
			break;
		}
		case cga::block_type::receive:
		{
			status = mdb_get (env.tx (transaction_a), receive_blocks, cga::mdb_val (hash_a), value);
			break;
		}
		case cga::block_type::open:
		{
			status = mdb_get (env.tx (transaction_a), open_blocks, cga::mdb_val (hash_a), value);
			break;
		}
		case cga::block_type::change:
		{
			status = mdb_get (env.tx (transaction_a), change_blocks, cga::mdb_val (hash_a), value);
			break;
		}
		case cga::block_type::state:
		{
			status = mdb_get (env.tx (transaction_a), state_blocks_v1, cga::mdb_val (hash_a), value);
			if (status != 0)
			{
				status = mdb_get (env.tx (transaction_a), state_blocks_v0, cga::mdb_val (hash_a), value);
			}
			break;
		}
		case cga::block_type::invalid:
		case cga::block_type::not_a_block:
		{
			break;
		}
	}

	release_assert (status == MDB_SUCCESS || status == MDB_NOTFOUND);
	boost::optional<MDB_val> result;
	if (status == MDB_SUCCESS)
	{
		result = value;
	}

	return result;
}

MDB_val cga::mdb_store::block_raw_get (cga::transaction const & transaction_a, cga::block_hash const & hash_a, cga::block_type & type_a)
{
	cga::mdb_val result;
	// Table lookups are ordered by match probability
	cga::block_type block_types[]{ cga::block_type::state, cga::block_type::send, cga::block_type::receive, cga::block_type::open, cga::block_type::change };
	for (auto current_type : block_types)
	{
		auto mdb_val (block_raw_get_by_type (transaction_a, hash_a, current_type));
		if (mdb_val.is_initialized ())
		{
			type_a = current_type;
			result = mdb_val.get ();
			break;
		}
	}

	return result;
}

template <typename T>
std::shared_ptr<cga::block> cga::mdb_store::block_random (cga::transaction const & transaction_a, MDB_dbi database)
{
	cga::block_hash hash;
	cga::random_pool::generate_block (hash.bytes.data (), hash.bytes.size ());
	cga::store_iterator<cga::block_hash, std::shared_ptr<T>> existing (std::make_unique<cga::mdb_iterator<cga::block_hash, std::shared_ptr<T>>> (transaction_a, database, cga::mdb_val (hash)));
	if (existing == cga::store_iterator<cga::block_hash, std::shared_ptr<T>> (nullptr))
	{
		existing = cga::store_iterator<cga::block_hash, std::shared_ptr<T>> (std::make_unique<cga::mdb_iterator<cga::block_hash, std::shared_ptr<T>>> (transaction_a, database));
	}
	auto end (cga::store_iterator<cga::block_hash, std::shared_ptr<T>> (nullptr));
	assert (existing != end);
	return block_get (transaction_a, cga::block_hash (existing->first));
}

std::shared_ptr<cga::block> cga::mdb_store::block_random (cga::transaction const & transaction_a)
{
	auto count (block_count (transaction_a));
	release_assert (std::numeric_limits<CryptoPP::word32>::max () > count.sum ());
	auto region = static_cast<size_t> (cga::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (count.sum () - 1)));
	std::shared_ptr<cga::block> result;
	if (region < count.send)
	{
		result = block_random<cga::send_block> (transaction_a, send_blocks);
	}
	else
	{
		region -= count.send;
		if (region < count.receive)
		{
			result = block_random<cga::receive_block> (transaction_a, receive_blocks);
		}
		else
		{
			region -= count.receive;
			if (region < count.open)
			{
				result = block_random<cga::open_block> (transaction_a, open_blocks);
			}
			else
			{
				region -= count.open;
				if (region < count.change)
				{
					result = block_random<cga::change_block> (transaction_a, change_blocks);
				}
				else
				{
					region -= count.change;
					if (region < count.state_v0)
					{
						result = block_random<cga::state_block> (transaction_a, state_blocks_v0);
					}
					else
					{
						result = block_random<cga::state_block> (transaction_a, state_blocks_v1);
					}
				}
			}
		}
	}
	assert (result != nullptr);
	return result;
}

bool cga::mdb_store::full_sideband (cga::transaction const & transaction_a)
{
	return version_get (transaction_a) > 12;
}

bool cga::mdb_store::entry_has_sideband (MDB_val entry_a, cga::block_type type_a)
{
	return entry_a.mv_size == cga::block::size (type_a) + cga::block_sideband::size (type_a);
}

size_t cga::mdb_store::block_successor_offset (cga::transaction const & transaction_a, MDB_val entry_a, cga::block_type type_a)
{
	size_t result;
	if (full_sideband (transaction_a) || entry_has_sideband (entry_a, type_a))
	{
		result = entry_a.mv_size - cga::block_sideband::size (type_a);
	}
	else
	{
		// Read old successor-only sideband
		assert (entry_a.mv_size = cga::block::size (type_a) + sizeof (cga::uint256_union));
		result = entry_a.mv_size - sizeof (cga::uint256_union);
	}
	return result;
}

cga::block_hash cga::mdb_store::block_successor (cga::transaction const & transaction_a, cga::block_hash const & hash_a)
{
	cga::block_type type;
	auto value (block_raw_get (transaction_a, hash_a, type));
	cga::block_hash result;
	if (value.mv_size != 0)
	{
		assert (value.mv_size >= result.bytes.size ());
		cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data) + block_successor_offset (transaction_a, value, type), result.bytes.size ());
		auto error (cga::try_read (stream, result.bytes));
		assert (!error);
	}
	else
	{
		result.clear ();
	}
	return result;
}

void cga::mdb_store::block_successor_clear (cga::transaction const & transaction_a, cga::block_hash const & hash_a)
{
	cga::block_type type;
	auto value (block_raw_get (transaction_a, hash_a, type));
	auto version (block_version (transaction_a, hash_a));
	assert (value.mv_size != 0);
	std::vector<uint8_t> data (static_cast<uint8_t *> (value.mv_data), static_cast<uint8_t *> (value.mv_data) + value.mv_size);
	std::fill_n (data.begin () + block_successor_offset (transaction_a, value, type), sizeof (cga::uint256_union), 0);
	block_raw_put (transaction_a, block_database (type, version), hash_a, cga::mdb_val (data.size (), data.data ()));
}

std::shared_ptr<cga::block> cga::mdb_store::block_get (cga::transaction const & transaction_a, cga::block_hash const & hash_a, cga::block_sideband * sideband_a)
{
	cga::block_type type;
	auto value (block_raw_get (transaction_a, hash_a, type));
	std::shared_ptr<cga::block> result;
	if (value.mv_size != 0)
	{
		cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
		result = cga::deserialize_block (stream, type);
		assert (result != nullptr);
		if (sideband_a)
		{
			sideband_a->type = type;
			if (full_sideband (transaction_a) || entry_has_sideband (value, type))
			{
				auto error (sideband_a->deserialize (stream));
				assert (!error);
			}
			else
			{
				// Reconstruct sideband data for block.
				sideband_a->account = block_account_computed (transaction_a, hash_a);
				sideband_a->balance = block_balance_computed (transaction_a, hash_a);
				sideband_a->successor = block_successor (transaction_a, hash_a);
				sideband_a->height = 0;
				sideband_a->timestamp = 0;
			}
		}
	}
	return result;
}

void cga::mdb_store::block_del (cga::transaction const & transaction_a, cga::block_hash const & hash_a)
{
	auto status (mdb_del (env.tx (transaction_a), state_blocks_v1, cga::mdb_val (hash_a), nullptr));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	if (status != 0)
	{
		auto status (mdb_del (env.tx (transaction_a), state_blocks_v0, cga::mdb_val (hash_a), nullptr));
		release_assert (status == 0 || status == MDB_NOTFOUND);
		if (status != 0)
		{
			auto status (mdb_del (env.tx (transaction_a), send_blocks, cga::mdb_val (hash_a), nullptr));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			if (status != 0)
			{
				auto status (mdb_del (env.tx (transaction_a), receive_blocks, cga::mdb_val (hash_a), nullptr));
				release_assert (status == 0 || status == MDB_NOTFOUND);
				if (status != 0)
				{
					auto status (mdb_del (env.tx (transaction_a), open_blocks, cga::mdb_val (hash_a), nullptr));
					release_assert (status == 0 || status == MDB_NOTFOUND);
					if (status != 0)
					{
						auto status (mdb_del (env.tx (transaction_a), change_blocks, cga::mdb_val (hash_a), nullptr));
						release_assert (status == 0);
					}
				}
			}
		}
	}
}

bool cga::mdb_store::block_exists (cga::transaction const & transaction_a, cga::block_type type, cga::block_hash const & hash_a)
{
	auto exists (false);
	cga::mdb_val junk;

	switch (type)
	{
		case cga::block_type::send:
		{
			auto status (mdb_get (env.tx (transaction_a), send_blocks, cga::mdb_val (hash_a), junk));
			assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			break;
		}
		case cga::block_type::receive:
		{
			auto status (mdb_get (env.tx (transaction_a), receive_blocks, cga::mdb_val (hash_a), junk));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			break;
		}
		case cga::block_type::open:
		{
			auto status (mdb_get (env.tx (transaction_a), open_blocks, cga::mdb_val (hash_a), junk));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			break;
		}
		case cga::block_type::change:
		{
			auto status (mdb_get (env.tx (transaction_a), change_blocks, cga::mdb_val (hash_a), junk));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			break;
		}
		case cga::block_type::state:
		{
			auto status (mdb_get (env.tx (transaction_a), state_blocks_v0, cga::mdb_val (hash_a), junk));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			if (!exists)
			{
				auto status (mdb_get (env.tx (transaction_a), state_blocks_v1, cga::mdb_val (hash_a), junk));
				release_assert (status == 0 || status == MDB_NOTFOUND);
				exists = status == 0;
			}
			break;
		}
		case cga::block_type::invalid:
		case cga::block_type::not_a_block:
			break;
	}

	return exists;
}

bool cga::mdb_store::block_exists (cga::transaction const & tx_a, cga::block_hash const & hash_a)
{
	// clang-format off
	return
		block_exists (tx_a, cga::block_type::send, hash_a) ||
		block_exists (tx_a, cga::block_type::receive, hash_a) ||
		block_exists (tx_a, cga::block_type::open, hash_a) ||
		block_exists (tx_a, cga::block_type::change, hash_a) ||
		block_exists (tx_a, cga::block_type::state, hash_a);
	// clang-format on
}

cga::block_counts cga::mdb_store::block_count (cga::transaction const & transaction_a)
{
	cga::block_counts result;
	MDB_stat send_stats;
	auto status1 (mdb_stat (env.tx (transaction_a), send_blocks, &send_stats));
	release_assert (status1 == 0);
	MDB_stat receive_stats;
	auto status2 (mdb_stat (env.tx (transaction_a), receive_blocks, &receive_stats));
	release_assert (status2 == 0);
	MDB_stat open_stats;
	auto status3 (mdb_stat (env.tx (transaction_a), open_blocks, &open_stats));
	release_assert (status3 == 0);
	MDB_stat change_stats;
	auto status4 (mdb_stat (env.tx (transaction_a), change_blocks, &change_stats));
	release_assert (status4 == 0);
	MDB_stat state_v0_stats;
	auto status5 (mdb_stat (env.tx (transaction_a), state_blocks_v0, &state_v0_stats));
	release_assert (status5 == 0);
	MDB_stat state_v1_stats;
	auto status6 (mdb_stat (env.tx (transaction_a), state_blocks_v1, &state_v1_stats));
	release_assert (status6 == 0);
	result.send = send_stats.ms_entries;
	result.receive = receive_stats.ms_entries;
	result.open = open_stats.ms_entries;
	result.change = change_stats.ms_entries;
	result.state_v0 = state_v0_stats.ms_entries;
	result.state_v1 = state_v1_stats.ms_entries;
	return result;
}

bool cga::mdb_store::root_exists (cga::transaction const & transaction_a, cga::uint256_union const & root_a)
{
	return block_exists (transaction_a, root_a) || account_exists (transaction_a, root_a);
}

bool cga::mdb_store::source_exists (cga::transaction const & transaction_a, cga::block_hash const & source_a)
{
	return block_exists (transaction_a, cga::block_type::state, source_a) || block_exists (transaction_a, cga::block_type::send, source_a);
}

cga::account cga::mdb_store::block_account (cga::transaction const & transaction_a, cga::block_hash const & hash_a)
{
	cga::block_sideband sideband;
	auto block (block_get (transaction_a, hash_a, &sideband));
	cga::account result (block->account ());
	if (result.is_zero ())
	{
		result = sideband.account;
	}
	assert (!result.is_zero ());
	return result;
}

// Return account containing hash
cga::account cga::mdb_store::block_account_computed (cga::transaction const & transaction_a, cga::block_hash const & hash_a)
{
	assert (!full_sideband (transaction_a));
	cga::account result (0);
	auto hash (hash_a);
	while (result.is_zero ())
	{
		auto block (block_get (transaction_a, hash));
		assert (block);
		result = block->account ();
		if (result.is_zero ())
		{
			auto type (cga::block_type::invalid);
			auto value (block_raw_get (transaction_a, block->previous (), type));
			if (entry_has_sideband (value, type))
			{
				result = block_account (transaction_a, block->previous ());
			}
			else
			{
				cga::block_info block_info;
				if (!block_info_get (transaction_a, hash, block_info))
				{
					result = block_info.account;
				}
				else
				{
					result = frontier_get (transaction_a, hash);
					if (result.is_zero ())
					{
						auto successor (block_successor (transaction_a, hash));
						assert (!successor.is_zero ());
						hash = successor;
					}
				}
			}
		}
	}
	assert (!result.is_zero ());
	return result;
}

void cga::mdb_store::account_del (cga::transaction const & transaction_a, cga::account const & account_a)
{
	auto status1 (mdb_del (env.tx (transaction_a), accounts_v1, cga::mdb_val (account_a), nullptr));
	if (status1 != 0)
	{
		release_assert (status1 == MDB_NOTFOUND);
		auto status2 (mdb_del (env.tx (transaction_a), accounts_v0, cga::mdb_val (account_a), nullptr));
		release_assert (status2 == 0);
	}
}

bool cga::mdb_store::account_exists (cga::transaction const & transaction_a, cga::account const & account_a)
{
	auto iterator (latest_begin (transaction_a, account_a));
	return iterator != latest_end () && cga::account (iterator->first) == account_a;
}

bool cga::mdb_store::account_get (cga::transaction const & transaction_a, cga::account const & account_a, cga::account_info & info_a)
{
	cga::mdb_val value;
	auto status1 (mdb_get (env.tx (transaction_a), accounts_v1, cga::mdb_val (account_a), value));
	release_assert (status1 == 0 || status1 == MDB_NOTFOUND);
	bool result (false);
	cga::epoch epoch;
	if (status1 == 0)
	{
		epoch = cga::epoch::epoch_1;
	}
	else
	{
		auto status2 (mdb_get (env.tx (transaction_a), accounts_v0, cga::mdb_val (account_a), value));
		release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
		if (status2 == 0)
		{
			epoch = cga::epoch::epoch_0;
		}
		else
		{
			result = true;
		}
	}
	if (!result)
	{
		cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		info_a.epoch = epoch;
		info_a.deserialize (stream);
	}
	return result;
}

void cga::mdb_store::frontier_put (cga::transaction const & transaction_a, cga::block_hash const & block_a, cga::account const & account_a)
{
	auto status (mdb_put (env.tx (transaction_a), frontiers, cga::mdb_val (block_a), cga::mdb_val (account_a), 0));
	release_assert (status == 0);
}

cga::account cga::mdb_store::frontier_get (cga::transaction const & transaction_a, cga::block_hash const & block_a)
{
	cga::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), frontiers, cga::mdb_val (block_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	cga::account result (0);
	if (status == 0)
	{
		result = cga::uint256_union (value);
	}
	return result;
}

void cga::mdb_store::frontier_del (cga::transaction const & transaction_a, cga::block_hash const & block_a)
{
	auto status (mdb_del (env.tx (transaction_a), frontiers, cga::mdb_val (block_a), nullptr));
	release_assert (status == 0);
}

size_t cga::mdb_store::account_count (cga::transaction const & transaction_a)
{
	MDB_stat stats1;
	auto status1 (mdb_stat (env.tx (transaction_a), accounts_v0, &stats1));
	release_assert (status1 == 0);
	MDB_stat stats2;
	auto status2 (mdb_stat (env.tx (transaction_a), accounts_v1, &stats2));
	release_assert (status2 == 0);
	auto result (stats1.ms_entries + stats2.ms_entries);
	return result;
}

void cga::mdb_store::account_put (cga::transaction const & transaction_a, cga::account const & account_a, cga::account_info const & info_a)
{
	MDB_dbi db;
	switch (info_a.epoch)
	{
		case cga::epoch::invalid:
		case cga::epoch::unspecified:
			assert (false);
		case cga::epoch::epoch_0:
			db = accounts_v0;
			break;
		case cga::epoch::epoch_1:
			db = accounts_v1;
			break;
	}
	auto status (mdb_put (env.tx (transaction_a), db, cga::mdb_val (account_a), cga::mdb_val (info_a), 0));
	release_assert (status == 0);
}

void cga::mdb_store::pending_put (cga::transaction const & transaction_a, cga::pending_key const & key_a, cga::pending_info const & pending_a)
{
	MDB_dbi db;
	switch (pending_a.epoch)
	{
		case cga::epoch::invalid:
		case cga::epoch::unspecified:
			assert (false);
		case cga::epoch::epoch_0:
			db = pending_v0;
			break;
		case cga::epoch::epoch_1:
			db = pending_v1;
			break;
	}
	auto status (mdb_put (env.tx (transaction_a), db, cga::mdb_val (key_a), cga::mdb_val (pending_a), 0));
	release_assert (status == 0);
}

void cga::mdb_store::pending_del (cga::transaction const & transaction_a, cga::pending_key const & key_a)
{
	auto status1 (mdb_del (env.tx (transaction_a), pending_v1, mdb_val (key_a), nullptr));
	if (status1 != 0)
	{
		release_assert (status1 == MDB_NOTFOUND);
		auto status2 (mdb_del (env.tx (transaction_a), pending_v0, mdb_val (key_a), nullptr));
		release_assert (status2 == 0);
	}
}

bool cga::mdb_store::pending_exists (cga::transaction const & transaction_a, cga::pending_key const & key_a)
{
	auto iterator (pending_begin (transaction_a, key_a));
	return iterator != pending_end () && cga::pending_key (iterator->first) == key_a;
}

bool cga::mdb_store::pending_get (cga::transaction const & transaction_a, cga::pending_key const & key_a, cga::pending_info & pending_a)
{
	cga::mdb_val value;
	auto status1 (mdb_get (env.tx (transaction_a), pending_v1, mdb_val (key_a), value));
	release_assert (status1 == 0 || status1 == MDB_NOTFOUND);
	bool result (false);
	cga::epoch epoch;
	if (status1 == 0)
	{
		epoch = cga::epoch::epoch_1;
	}
	else
	{
		auto status2 (mdb_get (env.tx (transaction_a), pending_v0, mdb_val (key_a), value));
		release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
		if (status2 == 0)
		{
			epoch = cga::epoch::epoch_0;
		}
		else
		{
			result = true;
		}
	}
	if (!result)
	{
		cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		pending_a.epoch = epoch;
		pending_a.deserialize (stream);
	}
	return result;
}

cga::store_iterator<cga::pending_key, cga::pending_info> cga::mdb_store::pending_begin (cga::transaction const & transaction_a, cga::pending_key const & key_a)
{
	cga::store_iterator<cga::pending_key, cga::pending_info> result (std::make_unique<cga::mdb_merge_iterator<cga::pending_key, cga::pending_info>> (transaction_a, pending_v0, pending_v1, mdb_val (key_a)));
	return result;
}

cga::store_iterator<cga::pending_key, cga::pending_info> cga::mdb_store::pending_begin (cga::transaction const & transaction_a)
{
	cga::store_iterator<cga::pending_key, cga::pending_info> result (std::make_unique<cga::mdb_merge_iterator<cga::pending_key, cga::pending_info>> (transaction_a, pending_v0, pending_v1));
	return result;
}

cga::store_iterator<cga::pending_key, cga::pending_info> cga::mdb_store::pending_end ()
{
	cga::store_iterator<cga::pending_key, cga::pending_info> result (nullptr);
	return result;
}

cga::store_iterator<cga::pending_key, cga::pending_info> cga::mdb_store::pending_v0_begin (cga::transaction const & transaction_a, cga::pending_key const & key_a)
{
	cga::store_iterator<cga::pending_key, cga::pending_info> result (std::make_unique<cga::mdb_iterator<cga::pending_key, cga::pending_info>> (transaction_a, pending_v0, mdb_val (key_a)));
	return result;
}

cga::store_iterator<cga::pending_key, cga::pending_info> cga::mdb_store::pending_v0_begin (cga::transaction const & transaction_a)
{
	cga::store_iterator<cga::pending_key, cga::pending_info> result (std::make_unique<cga::mdb_iterator<cga::pending_key, cga::pending_info>> (transaction_a, pending_v0));
	return result;
}

cga::store_iterator<cga::pending_key, cga::pending_info> cga::mdb_store::pending_v0_end ()
{
	cga::store_iterator<cga::pending_key, cga::pending_info> result (nullptr);
	return result;
}

cga::store_iterator<cga::pending_key, cga::pending_info> cga::mdb_store::pending_v1_begin (cga::transaction const & transaction_a, cga::pending_key const & key_a)
{
	cga::store_iterator<cga::pending_key, cga::pending_info> result (std::make_unique<cga::mdb_iterator<cga::pending_key, cga::pending_info>> (transaction_a, pending_v1, mdb_val (key_a)));
	return result;
}

cga::store_iterator<cga::pending_key, cga::pending_info> cga::mdb_store::pending_v1_begin (cga::transaction const & transaction_a)
{
	cga::store_iterator<cga::pending_key, cga::pending_info> result (std::make_unique<cga::mdb_iterator<cga::pending_key, cga::pending_info>> (transaction_a, pending_v1));
	return result;
}

cga::store_iterator<cga::pending_key, cga::pending_info> cga::mdb_store::pending_v1_end ()
{
	cga::store_iterator<cga::pending_key, cga::pending_info> result (nullptr);
	return result;
}

bool cga::mdb_store::block_info_get (cga::transaction const & transaction_a, cga::block_hash const & hash_a, cga::block_info & block_info_a)
{
	assert (!full_sideband (transaction_a));
	cga::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), blocks_info, cga::mdb_val (hash_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	bool result (true);
	if (status != MDB_NOTFOUND)
	{
		result = false;
		assert (value.size () == sizeof (block_info_a.account.bytes) + sizeof (block_info_a.balance.bytes));
		cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error1 (cga::try_read (stream, block_info_a.account));
		assert (!error1);
		auto error2 (cga::try_read (stream, block_info_a.balance));
		assert (!error2);
	}
	return result;
}

cga::uint128_t cga::mdb_store::representation_get (cga::transaction const & transaction_a, cga::account const & account_a)
{
	cga::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), representation, cga::mdb_val (account_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	cga::uint128_t result = 0;
	if (status == 0)
	{
		cga::uint128_union rep;
		cga::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error (cga::try_read (stream, rep));
		assert (!error);
		result = rep.number ();
	}
	return result;
}

void cga::mdb_store::representation_put (cga::transaction const & transaction_a, cga::account const & account_a, cga::uint128_t const & representation_a)
{
	cga::uint128_union rep (representation_a);
	auto status (mdb_put (env.tx (transaction_a), representation, cga::mdb_val (account_a), cga::mdb_val (rep), 0));
	release_assert (status == 0);
}

void cga::mdb_store::unchecked_clear (cga::transaction const & transaction_a)
{
	auto status (mdb_drop (env.tx (transaction_a), unchecked, 0));
	release_assert (status == 0);
}

void cga::mdb_store::unchecked_put (cga::transaction const & transaction_a, cga::unchecked_key const & key_a, cga::unchecked_info const & info_a)
{
	auto status (mdb_put (env.tx (transaction_a), unchecked, cga::mdb_val (key_a), cga::mdb_val (info_a), 0));
	release_assert (status == 0);
}

void cga::mdb_store::unchecked_put (cga::transaction const & transaction_a, cga::block_hash const & hash_a, std::shared_ptr<cga::block> const & block_a)
{
	cga::unchecked_key key (hash_a, block_a->hash ());
	cga::unchecked_info info (block_a, block_a->account (), cga::seconds_since_epoch (), cga::signature_verification::unknown);
	unchecked_put (transaction_a, key, info);
}

std::shared_ptr<cga::vote> cga::mdb_store::vote_get (cga::transaction const & transaction_a, cga::account const & account_a)
{
	cga::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), vote, cga::mdb_val (account_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	if (status == 0)
	{
		std::shared_ptr<cga::vote> result (value);
		assert (result != nullptr);
		return result;
	}
	return nullptr;
}

std::vector<cga::unchecked_info> cga::mdb_store::unchecked_get (cga::transaction const & transaction_a, cga::block_hash const & hash_a)
{
	std::vector<cga::unchecked_info> result;
	for (auto i (unchecked_begin (transaction_a, cga::unchecked_key (hash_a, 0))), n (unchecked_end ()); i != n && cga::block_hash (i->first.key ()) == hash_a; ++i)
	{
		cga::unchecked_info unchecked_info (i->second);
		result.push_back (unchecked_info);
	}
	return result;
}

bool cga::mdb_store::unchecked_exists (cga::transaction const & transaction_a, cga::unchecked_key const & key_a)
{
	auto iterator (unchecked_begin (transaction_a, key_a));
	return iterator != unchecked_end () && cga::unchecked_key (iterator->first) == key_a;
}

void cga::mdb_store::unchecked_del (cga::transaction const & transaction_a, cga::unchecked_key const & key_a)
{
	auto status (mdb_del (env.tx (transaction_a), unchecked, cga::mdb_val (key_a), nullptr));
	release_assert (status == 0 || status == MDB_NOTFOUND);
}

size_t cga::mdb_store::unchecked_count (cga::transaction const & transaction_a)
{
	MDB_stat unchecked_stats;
	auto status (mdb_stat (env.tx (transaction_a), unchecked, &unchecked_stats));
	release_assert (status == 0);
	auto result (unchecked_stats.ms_entries);
	return result;
}

void cga::mdb_store::online_weight_put (cga::transaction const & transaction_a, uint64_t time_a, cga::amount const & amount_a)
{
	auto status (mdb_put (env.tx (transaction_a), online_weight, cga::mdb_val (time_a), cga::mdb_val (amount_a), 0));
	release_assert (status == 0);
}

void cga::mdb_store::online_weight_del (cga::transaction const & transaction_a, uint64_t time_a)
{
	auto status (mdb_del (env.tx (transaction_a), online_weight, cga::mdb_val (time_a), nullptr));
	release_assert (status == 0);
}

cga::store_iterator<uint64_t, cga::amount> cga::mdb_store::online_weight_begin (cga::transaction const & transaction_a)
{
	return cga::store_iterator<uint64_t, cga::amount> (std::make_unique<cga::mdb_iterator<uint64_t, cga::amount>> (transaction_a, online_weight));
}

cga::store_iterator<uint64_t, cga::amount> cga::mdb_store::online_weight_end ()
{
	return cga::store_iterator<uint64_t, cga::amount> (nullptr);
}

size_t cga::mdb_store::online_weight_count (cga::transaction const & transaction_a) const
{
	MDB_stat online_weight_stats;
	auto status1 (mdb_stat (env.tx (transaction_a), online_weight, &online_weight_stats));
	release_assert (status1 == 0);
	return online_weight_stats.ms_entries;
}

void cga::mdb_store::online_weight_clear (cga::transaction const & transaction_a)
{
	auto status (mdb_drop (env.tx (transaction_a), online_weight, 0));
	release_assert (status == 0);
}

void cga::mdb_store::flush (cga::transaction const & transaction_a)
{
	{
		std::lock_guard<std::mutex> lock (cache_mutex);
		vote_cache_l1.swap (vote_cache_l2);
		vote_cache_l1.clear ();
	}
	for (auto i (vote_cache_l2.begin ()), n (vote_cache_l2.end ()); i != n; ++i)
	{
		std::vector<uint8_t> vector;
		{
			cga::vectorstream stream (vector);
			i->second->serialize (stream);
		}
		auto status1 (mdb_put (env.tx (transaction_a), vote, cga::mdb_val (i->first), cga::mdb_val (vector.size (), vector.data ()), 0));
		release_assert (status1 == 0);
	}
}
std::shared_ptr<cga::vote> cga::mdb_store::vote_current (cga::transaction const & transaction_a, cga::account const & account_a)
{
	assert (!cache_mutex.try_lock ());
	std::shared_ptr<cga::vote> result;
	auto existing (vote_cache_l1.find (account_a));
	auto have_existing (true);
	if (existing == vote_cache_l1.end ())
	{
		existing = vote_cache_l2.find (account_a);
		if (existing == vote_cache_l2.end ())
		{
			have_existing = false;
		}
	}
	if (have_existing)
	{
		result = existing->second;
	}
	else
	{
		result = vote_get (transaction_a, account_a);
	}
	return result;
}

std::shared_ptr<cga::vote> cga::mdb_store::vote_generate (cga::transaction const & transaction_a, cga::account const & account_a, cga::raw_key const & key_a, std::shared_ptr<cga::block> block_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto result (vote_current (transaction_a, account_a));
	uint64_t sequence ((result ? result->sequence : 0) + 1);
	result = std::make_shared<cga::vote> (account_a, key_a, sequence, block_a);
	vote_cache_l1[account_a] = result;
	return result;
}

std::shared_ptr<cga::vote> cga::mdb_store::vote_generate (cga::transaction const & transaction_a, cga::account const & account_a, cga::raw_key const & key_a, std::vector<cga::block_hash> blocks_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto result (vote_current (transaction_a, account_a));
	uint64_t sequence ((result ? result->sequence : 0) + 1);
	result = std::make_shared<cga::vote> (account_a, key_a, sequence, blocks_a);
	vote_cache_l1[account_a] = result;
	return result;
}

std::shared_ptr<cga::vote> cga::mdb_store::vote_max (cga::transaction const & transaction_a, std::shared_ptr<cga::vote> vote_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto current (vote_current (transaction_a, vote_a->account));
	auto result (vote_a);
	if (current != nullptr && current->sequence > result->sequence)
	{
		result = current;
	}
	vote_cache_l1[vote_a->account] = result;
	return result;
}

cga::store_iterator<cga::account, cga::account_info> cga::mdb_store::latest_begin (cga::transaction const & transaction_a, cga::account const & account_a)
{
	cga::store_iterator<cga::account, cga::account_info> result (std::make_unique<cga::mdb_merge_iterator<cga::account, cga::account_info>> (transaction_a, accounts_v0, accounts_v1, cga::mdb_val (account_a)));
	return result;
}

cga::store_iterator<cga::account, cga::account_info> cga::mdb_store::latest_begin (cga::transaction const & transaction_a)
{
	cga::store_iterator<cga::account, cga::account_info> result (std::make_unique<cga::mdb_merge_iterator<cga::account, cga::account_info>> (transaction_a, accounts_v0, accounts_v1));
	return result;
}

cga::store_iterator<cga::account, cga::account_info> cga::mdb_store::latest_end ()
{
	cga::store_iterator<cga::account, cga::account_info> result (nullptr);
	return result;
}

cga::store_iterator<cga::account, cga::account_info> cga::mdb_store::latest_v0_begin (cga::transaction const & transaction_a, cga::account const & account_a)
{
	cga::store_iterator<cga::account, cga::account_info> result (std::make_unique<cga::mdb_iterator<cga::account, cga::account_info>> (transaction_a, accounts_v0, cga::mdb_val (account_a)));
	return result;
}

cga::store_iterator<cga::account, cga::account_info> cga::mdb_store::latest_v0_begin (cga::transaction const & transaction_a)
{
	cga::store_iterator<cga::account, cga::account_info> result (std::make_unique<cga::mdb_iterator<cga::account, cga::account_info>> (transaction_a, accounts_v0));
	return result;
}

cga::store_iterator<cga::account, cga::account_info> cga::mdb_store::latest_v0_end ()
{
	cga::store_iterator<cga::account, cga::account_info> result (nullptr);
	return result;
}

cga::store_iterator<cga::account, cga::account_info> cga::mdb_store::latest_v1_begin (cga::transaction const & transaction_a, cga::account const & account_a)
{
	cga::store_iterator<cga::account, cga::account_info> result (std::make_unique<cga::mdb_iterator<cga::account, cga::account_info>> (transaction_a, accounts_v1, cga::mdb_val (account_a)));
	return result;
}

cga::store_iterator<cga::account, cga::account_info> cga::mdb_store::latest_v1_begin (cga::transaction const & transaction_a)
{
	cga::store_iterator<cga::account, cga::account_info> result (std::make_unique<cga::mdb_iterator<cga::account, cga::account_info>> (transaction_a, accounts_v1));
	return result;
}

cga::store_iterator<cga::account, cga::account_info> cga::mdb_store::latest_v1_end ()
{
	cga::store_iterator<cga::account, cga::account_info> result (nullptr);
	return result;
}
