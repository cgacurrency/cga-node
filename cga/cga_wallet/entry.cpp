#include <cga/lib/errors.hpp>
#include <cga/lib/jsonconfig.hpp>
#include <cga/lib/utility.hpp>
#include <cga/cga_wallet/icon.hpp>
#include <cga/node/cli.hpp>
#include <cga/node/ipc.hpp>
#include <cga/node/rpc.hpp>
#include <cga/node/working.hpp>
#include <cga/qt/qt.hpp>

#include <boost/make_shared.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

class qt_wallet_config
{
public:
	qt_wallet_config (boost::filesystem::path const & application_path_a) :
	account (0),
	rpc_enable (false),
	opencl_enable (false)
	{
		cga::random_pool::generate_block (wallet.bytes.data (), wallet.bytes.size ());
		assert (!wallet.is_zero ());
	}
	bool upgrade_json (unsigned version_a, cga::jsonconfig & json)
	{
		json.put ("version", json_version ());
		auto upgraded (false);
		switch (version_a)
		{
			case 1:
			{
				cga::account account;
				account.decode_account (json.get<std::string> ("account"));
				json.erase ("account");
				json.put ("account", account.to_account ());
				json.erase ("version");
				upgraded = true;
			}
			case 2:
			{
				cga::jsonconfig rpc_l;
				rpc.serialize_json (rpc_l);
				json.put ("rpc_enable", "true");
				json.put_child ("rpc", rpc_l);
				json.erase ("version");
				upgraded = true;
			}
			case 3:
			{
				auto opencl_enable_l (json.get_optional<bool> ("opencl_enable"));
				if (!opencl_enable_l)
				{
					json.put ("opencl_enable", "false");
				}
				auto opencl_l (json.get_optional_child ("opencl"));
				if (!opencl_l)
				{
					cga::jsonconfig opencl_l;
					opencl.serialize_json (opencl_l);
					json.put_child ("opencl", opencl_l);
				}
				upgraded = true;
			}
			case 4:
				break;
			default:
				throw std::runtime_error ("Unknown qt_wallet_config version");
		}
		return upgraded;
	}

	cga::error deserialize_json (bool & upgraded_a, cga::jsonconfig & json)
	{
		if (!json.empty ())
		{
			auto version_l (json.get_optional<unsigned> ("version"));
			if (!version_l)
			{
				version_l = 1;
				json.put ("version", version_l.get ());
				upgraded_a = true;
			}
			upgraded_a |= upgrade_json (version_l.get (), json);
			auto wallet_l (json.get<std::string> ("wallet"));
			auto account_l (json.get<std::string> ("account"));
			auto node_l (json.get_required_child ("node"));
			rpc_enable = json.get<bool> ("rpc_enable");
			auto rpc_l (json.get_required_child ("rpc"));
			opencl_enable = json.get<bool> ("opencl_enable");
			auto opencl_l (json.get_required_child ("opencl"));

			if (wallet.decode_hex (wallet_l))
			{
				json.get_error ().set ("Invalid wallet id. Did you open a node daemon config?");
			}
			else if (account.decode_account (account_l))
			{
				json.get_error ().set ("Invalid account");
			}
			if (!node_l.get_error ())
			{
				node.deserialize_json (upgraded_a, node_l);
			}
			if (!rpc_l.get_error ())
			{
				rpc.deserialize_json (rpc_l);
			}
			if (!opencl_l.get_error ())
			{
				opencl.deserialize_json (opencl_l);
			}
			if (wallet.is_zero ())
			{
				cga::random_pool::generate_block (wallet.bytes.data (), wallet.bytes.size ());
				upgraded_a = true;
			}
		}
		else
		{
			serialize_json (json);
			upgraded_a = true;
		}
		return json.get_error ();
	}

	void serialize_json (cga::jsonconfig & json)
	{
		std::string wallet_string;
		wallet.encode_hex (wallet_string);
		json.put ("version", json_version ());
		json.put ("wallet", wallet_string);
		json.put ("account", account.to_account ());
		cga::jsonconfig node_l;
		node.enable_voting = false;
		node.bootstrap_connections_max = 4;
		node.serialize_json (node_l);
		json.put_child ("node", node_l);
		cga::jsonconfig rpc_l;
		rpc.serialize_json (rpc_l);
		json.put_child ("rpc", rpc_l);
		json.put ("rpc_enable", rpc_enable);
		json.put ("opencl_enable", opencl_enable);
		cga::jsonconfig opencl_l;
		opencl.serialize_json (opencl_l);
		json.put_child ("opencl", opencl_l);
	}

	bool serialize_json_stream (std::ostream & stream_a)
	{
		auto result (false);
		stream_a.seekp (0);
		try
		{
			cga::jsonconfig json;
			serialize_json (json);
			json.write (stream_a);
		}
		catch (std::runtime_error const & ex)
		{
			std::cerr << ex.what () << std::endl;
			result = true;
		}
		return result;
	}

	cga::uint256_union wallet;
	cga::account account;
	cga::node_config node;
	bool rpc_enable;
	cga::rpc_config rpc;
	bool opencl_enable;
	cga::opencl_config opencl;
	int json_version () const
	{
		return 4;
	}
};

namespace
{
void show_error (std::string const & message_a)
{
	QMessageBox message (QMessageBox::Critical, "Error starting Nano", message_a.c_str ());
	message.setModal (true);
	message.show ();
	message.exec ();
}
bool update_config (qt_wallet_config & config_a, boost::filesystem::path const & config_path_a)
{
	auto account (config_a.account);
	auto wallet (config_a.wallet);
	auto error (false);
	cga::jsonconfig config;
	if (!config.read_and_update (config_a, config_path_a))
	{
		if (account != config_a.account || wallet != config_a.wallet)
		{
			config_a.account = account;
			config_a.wallet = wallet;

			// Update json file with new account and/or wallet values
			std::fstream config_file;
			config_file.open (config_path_a.string (), std::ios_base::out | std::ios_base::trunc);
			error = config_a.serialize_json_stream (config_file);
		}
	}
	return error;
}
}

int run_wallet (QApplication & application, int argc, char * const * argv, boost::filesystem::path const & data_path, cga::node_flags const & flags)
{
	cga_qt::eventloop_processor processor;
	boost::system::error_code error_chmod;
	boost::filesystem::create_directories (data_path);
	cga::set_secure_perm_directory (data_path, error_chmod);
	QPixmap pixmap (":/logo.png");
	QSplashScreen * splash = new QSplashScreen (pixmap);
	splash->show ();
	application.processEvents ();
	splash->showMessage (QSplashScreen::tr ("Remember - Back Up Your Wallet Seed"), Qt::AlignBottom | Qt::AlignHCenter, Qt::darkGray);
	application.processEvents ();
	qt_wallet_config config (data_path);
	auto config_path ((data_path / "config.json"));
	int result (0);
	cga::jsonconfig json;
	auto error (json.read_and_update (config, config_path));
	cga::set_secure_perm_file (config_path, error_chmod);
	if (!error)
	{
		boost::asio::io_context io_ctx;
		config.node.logging.init (data_path);
		std::shared_ptr<cga::node> node;
		std::shared_ptr<cga_qt::wallet> gui;
		cga::set_application_icon (application);
		auto opencl (cga::opencl_work::create (config.opencl_enable, config.opencl, config.node.logging));
		cga::work_pool work (config.node.work_threads, opencl ? [&opencl](cga::uint256_union const & root_a) {
			return opencl->generate_work (root_a);
		}
		                                                       : std::function<boost::optional<uint64_t> (cga::uint256_union const &)> (nullptr));
		cga::alarm alarm (io_ctx);
		cga::node_init init;
		node = std::make_shared<cga::node> (init, io_ctx, data_path, alarm, config.node, work, flags);
		if (!init.error ())
		{
			auto wallet (node->wallets.open (config.wallet));
			if (wallet == nullptr)
			{
				auto existing (node->wallets.items.begin ());
				if (existing != node->wallets.items.end ())
				{
					wallet = existing->second;
					config.wallet = existing->first;
				}
				else
				{
					wallet = node->wallets.create (config.wallet);
				}
			}
			if (config.account.is_zero () || !wallet->exists (config.account))
			{
				auto transaction (wallet->wallets.tx_begin (true));
				auto existing (wallet->store.begin (transaction));
				if (existing != wallet->store.end ())
				{
					cga::uint256_union account (existing->first);
					config.account = account;
				}
				else
				{
					config.account = wallet->deterministic_insert (transaction);
				}
			}
			assert (wallet->exists (config.account));
			update_config (config, config_path);
			node->start ();
			std::unique_ptr<cga::rpc> rpc = get_rpc (io_ctx, *node, config.rpc);
			if (rpc)
			{
				rpc->start (config.rpc_enable);
			}
			cga::ipc::ipc_server ipc (*node, *rpc);
			cga::thread_runner runner (io_ctx, node->config.io_threads);
			QObject::connect (&application, &QApplication::aboutToQuit, [&]() {
				ipc.stop ();
				rpc->stop ();
				node->stop ();
			});
			application.postEvent (&processor, new cga_qt::eventloop_event ([&]() {
				gui = std::make_shared<cga_qt::wallet> (application, processor, *node, wallet, config.account);
				splash->close ();
				gui->start ();
				gui->client_window->show ();
			}));
			result = application.exec ();
			runner.join ();
		}
		else
		{
			splash->hide ();
			show_error ("Error initializing node");
		}
		update_config (config, config_path);
	}
	else
	{
		splash->hide ();
		show_error ("Error deserializing config: " + json.get_error ().get_message ());
	}
	return result;
}

int main (int argc, char * const * argv)
{
	cga::set_umask ();

	try
	{
		QApplication application (argc, const_cast<char **> (argv));
		boost::program_options::options_description description ("Command line options");
		description.add_options () ("help", "Print out options");
		cga::add_node_options (description);
		boost::program_options::variables_map vm;
		boost::program_options::store (boost::program_options::command_line_parser (argc, argv).options (description).allow_unregistered ().run (), vm);
		boost::program_options::notify (vm);
		int result (0);

		if (!vm.count ("data_path"))
		{
			std::string error_string;
			if (!cga::migrate_working_path (error_string))
			{
				throw std::runtime_error (error_string);
			}
		}

		auto ec = cga::handle_node_options (vm);
		if (ec == cga::error_cli::unknown_command)
		{
			if (vm.count ("help") != 0)
			{
				std::cout << description << std::endl;
			}
			else
			{
				try
				{
					boost::filesystem::path data_path;
					if (vm.count ("data_path"))
					{
						auto name (vm["data_path"].as<std::string> ());
						data_path = boost::filesystem::path (name);
					}
					else
					{
						data_path = cga::working_path ();
					}
					cga::node_flags flags;
					auto batch_size_it = vm.find ("batch_size");
					if (batch_size_it != vm.end ())
					{
						flags.sideband_batch_size = batch_size_it->second.as<size_t> ();
					}
					flags.disable_backup = (vm.count ("disable_backup") > 0);
					flags.disable_lazy_bootstrap = (vm.count ("disable_lazy_bootstrap") > 0);
					flags.disable_legacy_bootstrap = (vm.count ("disable_legacy_bootstrap") > 0);
					flags.disable_wallet_bootstrap = (vm.count ("disable_wallet_bootstrap") > 0);
					flags.disable_bootstrap_listener = (vm.count ("disable_bootstrap_listener") > 0);
					flags.disable_unchecked_cleanup = (vm.count ("disable_unchecked_cleanup") > 0);
					flags.disable_unchecked_drop = (vm.count ("disable_unchecked_drop") > 0);
					flags.fast_bootstrap = (vm.count ("fast_bootstrap") > 0);
					result = run_wallet (application, argc, argv, data_path, flags);
				}
				catch (std::exception const & e)
				{
					show_error (boost::str (boost::format ("Exception while running wallet: %1%") % e.what ()));
				}
				catch (...)
				{
					show_error ("Unknown exception while running wallet");
				}
			}
		}
		return result;
	}
	catch (std::exception const & e)
	{
		std::cerr << boost::str (boost::format ("Exception while initializing %1%") % e.what ());
	}
	catch (...)
	{
		std::cerr << boost::str (boost::format ("Unknown exception while initializing"));
	}
	return 1;
}
