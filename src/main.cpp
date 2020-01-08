#include <httplib.h>
#include <stdio.h>

int main(int argc, char** argv)
{
	httplib::Server server;

	bool result = server.set_base_dir("./data/public");
	if (!result) {
		fprintf(stderr, "set_base_dir failed\n");
		return 1;
	}

	server.listen("localhost", 6060);

	return 0;
}