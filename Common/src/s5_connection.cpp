#include "s5_connection.h"

S5Connection::S5Connection():ref_count(0),state(0),on_destroy(NULL)
{
}

S5Connection::~S5Connection()
{

}

int S5Connection::close()
{
	if (__sync_val_compare_and_swap(&state, CONN_OK, CONN_CLOSED) != CONN_OK)
	{
		return 0;//connection already closed
	}

	S5LOG_DEBUG("close connection conn:%p, %s", this, connection_info.c_str());
	do_close();
	if(on_close)
		on_close(this);
	return 0;
}