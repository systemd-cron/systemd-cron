#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <fcntl.h>
#include <errno.h>
#include <bits/local_lim.h>
#include <sys/stat.h>
#define	MAX_COMMAND	1000
#define	MAX_LINES	1000

void end(char * msg){
	fprintf(stderr,"ERROR: %s,aborting\n", msg);
	exit(1);
}

int main(int argc, char *argv[]) {
	if (argc != 2) end("bad argument");

	struct passwd *pw;
	pw = getpwuid(getuid());
	if (!pw) end("user doesn't exist");

	char crontab[sizeof CRONTAB_DIR + LOGIN_NAME_MAX];
	snprintf(crontab, sizeof crontab, "%s/%s", CRONTAB_DIR, pw->pw_name);
	FILE *file;

	char buffer[MAX_COMMAND];

	switch(argv[1][0]) {
		case 'r':
			file = fopen(crontab, "r");
			if (file == NULL) {
				if(errno == ENOENT) return ENOENT;
				perror("Cannot open input file");
				return 1;
			};
			while(!feof(file)) {
				if (!fgets(buffer, sizeof(buffer), file)) break;
				printf("%s", buffer);
			}
			fclose(file);
			break;
		case 'w':
			file = fopen(crontab, "w");
			if (file == NULL) {
				perror("Cannot open output file");
				return 1;
			}
			int lines=0;
			while(!feof(stdin)) {
				if (!fgets(buffer, sizeof(buffer), stdin)) break;
				lines++;
				if (fprintf(file, "%s", buffer) < 0) {
					perror("Cannot write to file");
					return 1;
				};
				if (lines > MAX_LINES) end("maximum lines reached");
			}
			fchown(fileno(file),getuid(),0);
			fchmod(fileno(file),0600);
			fclose(file);
			break;
		case 'd':
			if (unlink(crontab) == -1) {
				if(errno == ENOENT) {
					fprintf(stderr, "no crontab for %s\n", pw->pw_name);
					return 0;
				}
				perror("Cannot delete file");
				return 1;
			};
			break;
		default:
			end("unknown action");
			return 1;
	}
	return 0;
}
