#include "s5_connection.h"

S5Connection::S5Connection():ref_count(0),state(0),on_destroy(NULL)
{
}

S5Connection::~S5Connection()
{

}
