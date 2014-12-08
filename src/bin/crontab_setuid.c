#include <stdio.h>

int main(int argc, char *argv[]) {
	printf("CRONTAB_DIR=%s\n", CRONTAB_DIR);
	if (argc != 2) {
		fprintf(stderr,"ERROR\n");
		return 1;
	}
	switch(argv[1][0]) {
		case 'r':
			printf("READ\n");
			break;
		case 'w':
			printf("WRITE\n");
			break;
		case 'd':
			printf("DELETE\n");
			break;
		default:
			fprintf(stderr,"ERROR\n");
			return 1;
	}
	return 0;
}
