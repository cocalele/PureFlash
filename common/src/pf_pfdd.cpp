#define _ISOC99_SOURCE //to use strtoll
#include <semaphore.h>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "cxxopts.hpp"
#include "pf_client_api.h"
#include "pf_log.h"
#include "pf_utils.h"

using namespace  std;
void unexpected_exit_handler();
#define FIX_ISAL_LINK_PROBLEM 1
#ifdef FIX_ISAL_LINK_PROBLEM
#include "raid.h"
#include "crc.h"
#include "crc64.h"
#define TEST_SOURCES 16
#define TEST_LEN     16*1024


int _fake_use_xor_gen()
{

	int i, j, should_fail;
	void* buffs[TEST_SOURCES + 1];


	xor_gen(TEST_SOURCES + 1, TEST_LEN, buffs);
	crc32_iscsi(NULL, 0, 0);
	crc64_rocksoft_refl(0, NULL, 0);
	return 0;
}
#endif
void
parse(int argc, char* argv[])
{
	try
	{
		cxxopts::Options options(argv[0], " - example command line options");
		options
				.positional_help("[optional args]")
				.show_positional_help();

		bool apple = false;

		options
				.allow_unrecognised_options()
				.add_options()
//						("a,apple", "an apple", cxxopts::value<bool>(apple))
//						("b,bob", "Bob")
//						("char", "A character", cxxopts::value<char>())
//						("t,true", "True", cxxopts::value<bool>()->default_value("true"))
//						("f, file", "File", cxxopts::value<std::vector<std::string>>(), "FILE")
//						("i,input", "Input", cxxopts::value<std::string>())
//						("o,output", "Output file", cxxopts::value<std::string>()
//								->default_value("a.out")->implicit_value("b.def"), "BIN")
//						("positional",
//						 "Positional arguments: these are the arguments that are entered "
//						 "without an option", cxxopts::value<std::vector<std::string>>())
//						("long-description",
//						 "thisisareallylongwordthattakesupthewholelineandcannotbebrokenataspace")
//						("help", "Print help")
//						("int", "An integer", cxxopts::value<int>(), "N")
//						("float", "A floating point number", cxxopts::value<float>())
//						("vector", "A list of doubles", cxxopts::value<std::vector<double>>())
//						("option_that_is_too_long_for_the_help", "A very long option")
//#ifdef CXXOPTS_USE_UNICODE
//			("unicode", u8"A help option with non-ascii: à. Here the size of the"
//        " string should be correct")
//#endif
				;

//		options.add_options("Group")
//				("c,compile", "compile")
//				("d,drop", "drop", cxxopts::value<std::vector<std::string>>());

		options.parse_positional({"input", "output", "positional"});

		auto result = options.parse(argc, argv);

		if (result.count("help"))
		{
			std::cout << options.help({"", "Group"}) << std::endl;
			exit(0);
		}

		if (apple)
		{
			std::cout << "Saw option ‘a’ " << result.count("a") << " times " <<
					  std::endl;
		}

		if (result.count("b"))
		{
			std::cout << "Saw option ‘b’" << std::endl;
		}

		if (result.count("char"))
		{
			std::cout << "Saw a character ‘" << result["char"].as<char>() << "’" << std::endl;
		}

		if (result.count("f"))
		{
			auto& ff = result["f"].as<std::vector<std::string>>();
			std::cout << "Files" << std::endl;
			for (const auto& f : ff)
			{
				std::cout << f << std::endl;
			}
		}

		if (result.count("input"))
		{
			std::cout << "Input = " << result["input"].as<std::string>()
					  << std::endl;
		}

		if (result.count("output"))
		{
			std::cout << "Output = " << result["output"].as<std::string>()
					  << std::endl;
		}

		if (result.count("positional"))
		{
			std::cout << "Positional = {";
			auto& v = result["positional"].as<std::vector<std::string>>();
			for (const auto& s : v) {
				std::cout << s << ", ";
			}
			std::cout << "}" << std::endl;
		}

		if (result.count("int"))
		{
			std::cout << "int = " << result["int"].as<int>() << std::endl;
		}

		if (result.count("float"))
		{
			std::cout << "float = " << result["float"].as<float>() << std::endl;
		}

		if (result.count("vector"))
		{
			std::cout << "vector = ";
			const auto values = result["vector"].as<std::vector<double>>();
			for (const auto& v : values) {
				std::cout << v << ", ";
			}
			std::cout << std::endl;
		}

		std::cout << "Arguments remain = " << argc << std::endl;

		auto arguments = result.arguments();
		std::cout << "Saw " << arguments.size() << " arguments" << std::endl;
	}
	catch (const cxxopts::OptionException& e)
	{
		std::cout << "error parsing options: " << e.what() << std::endl;
		exit(1);
	}
}
struct io_waiter
{
	sem_t sem;
	int rc;
};

void io_cbk(void* cbk_arg, int complete_status)
{
	struct io_waiter* w = (struct io_waiter*)cbk_arg;
	w->rc = complete_status;
	sem_post(&w->sem);
}
static int64_t parseNumber(string str)
{
	if(str.length() == 1)
		return strtoll(str.c_str(), NULL, 10);
	int64_t l = strtoll(str.substr(0, str.length() - 1).c_str(), NULL, 10);
	switch(str.at(str.length()-1)){
		case 'k':
		case 'K':
			return l << 10;
		case 'm':
		case 'M':
			return l <<20;
		case 'g':
		case 'G':
			return l <<30;
		case 't':
		case 'T':
			return l <<40;
	}
	return strtoll(str.c_str(), NULL, 10);
}

//BOOL is_seekable(int fd)
//{
//	if(isatty(fd))
//		return FALSE;
//	struct stat st;
//	fstat(fd, &st);
//	if(S_ISFIFO(st.st_mode))
//		return FALSE;
//	return TRUE;
//}

int main(int argc, char* argv[])
{
	cxxopts::Options options(argv[0], " - PureFlash dd tool");
	string rw, bs_str, ifname, ofname, vol_name, cfg_file, snapshot_name;
	int count;
	off_t offset;

	std::set_terminate(unexpected_exit_handler);
	options.positional_help("[optional args]")
			.show_positional_help();
	options
			.add_options()
			("rw", "Read/Write", cxxopts::value<std::string>(rw), "read/write")
					("count", "Block count", cxxopts::value<int>(count)->default_value("1"))
					("bs", "Block size", cxxopts::value<string>(bs_str)->default_value("4k"))
					("if", "Input file name", cxxopts::value<string>(ifname))
					("of", "Output file name", cxxopts::value<string>(ofname))
					("c", "Config file name", cxxopts::value<string>(cfg_file)->default_value("/etc/pureflash/pf.conf"))
					("offset", "Offset in volume", cxxopts::value<off_t>(offset)->default_value("0"))
					("v", "Volume name", cxxopts::value<string>(vol_name))
					("snapshot", "Snapshot name to operate", cxxopts::value<string>(snapshot_name))
					("h,help", "Print usage")
					;
	if(argc == 1) {
		std::cout << options.help() << std::endl;
		exit(1);
	}
	try {
		auto result = options.parse(argc, argv);
		if (result.count("help")) {
			std::cout << options.help() << std::endl;
			exit(0);
		}
	}
	catch (const cxxopts::OptionException& e)
	{
		std::cout << "error parsing options: " << e.what() << std::endl;
		options.help();
		exit(1);
	}
	int64_t bs = parseNumber(bs_str);
	//TODO: need argments checking
	//int seekable = 1;
	void* buf = malloc(bs);
	DeferCall _c([buf](){free (buf);});
	int fd;
	int is_write = 0;
	if(rw == "write") {
		fd = open(ifname.c_str(), O_RDONLY);
		is_write = 1;
		//seekable = is_seekable(fd);
	} else if(rw == "read") {
		fd = open(ofname.c_str(), O_WRONLY|O_CREAT, 0666);
		is_write = 0;
		//seekable = is_seekable(fd);
	} else {
		S5LOG_FATAL("Invalid argument rw:%s", rw.c_str());
	}
	if(fd == -1) {
	    S5LOG_FATAL("Failed open file:%s, errno:%d", is_write ? ifname.c_str() : ofname.c_str(), errno);
    }
	DeferCall _c3([fd](){close(fd);});
	io_waiter arg;
	arg.rc = 0;
	sem_init(&arg.sem, 0, 0);
	struct PfClientVolume* vol = pf_open_volume(vol_name.c_str(), cfg_file.c_str(), snapshot_name.c_str(), S5_LIB_VER);
	if(vol == NULL) {
		S5LOG_FATAL("Failed open volume:%s", vol_name.c_str());
	}

	S5LOG_INFO("%s with block size:%d", is_write ? "Write":"Read", bs);
	int64_t offset_in_file = 0;
	for(int i=0;i<count;i++) {
		if(is_write == 0) {
			pf_io_submit(vol, buf, bs, offset + i * bs, io_cbk, &arg, is_write);
			sem_wait(&arg.sem);
			if(arg.rc != 0) {
				S5LOG_FATAL("Failed read data from volume, rc:%d", arg.rc);
			}
			ssize_t rc = ::write(fd, buf, bs);
			if(rc != bs) {
				S5LOG_FATAL("Failed write data to file, rc:%ld, errno:%d", rc, errno);
			}
			fsync(fd);

		} else {
			ssize_t rc =  ::read(fd, buf, bs);
			if(rc != bs) {
				S5LOG_FATAL("Failed read data from file, rc:%ld, errno:%d", rc, errno);
			}
			pf_io_submit(vol, buf, bs, offset + i * bs, io_cbk, &arg, is_write);
			sem_wait(&arg.sem);
			if(arg.rc != 0) {
				S5LOG_FATAL("Failed write data to volume, rc:%d", arg.rc);
			}
		}
	}
	S5LOG_INFO("Succeeded %s %d blocks", is_write ? "Write" : "Read", count);
	pf_close_volume(vol);
	S5LOG_INFO("Volume closed");

	return 0;
}
void unexpected_exit_handler()
{
	try { throw; }
	catch(const std::exception& e) {
		S5LOG_ERROR("Unhandled exception:%s", e.what());
	}
	catch(...) {
		S5LOG_ERROR("Unexpected exception");
	}
	exit(1);
}
