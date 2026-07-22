
#include "postgres.h"

#include "getopt_long.h"

int
main(int argc, char **argv)
{
	int c;
	const char		*shortopts = ":f:";
	struct option	 longopts[] = {
		{"log-filename", required_argument, NULL, 'f'},
		{NULL, 0, NULL, 0},
	};

	while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1)
	{
		if (c == ':')
			fprintf(stderr, "option '%s' requires an argument\n", argv[optind - 1]);
		else if (c == '?')
			fprintf(stderr, "unrecognized option '%s'\n", argv[optind - 1]);
	}

	return 0;
}
