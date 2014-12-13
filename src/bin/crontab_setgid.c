#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#define	MAX_COMMAND	1000
#define	MAX_LINES	1000

void end(char * msg){
	fprintf(stderr,"crontab_setgid: %s\n", msg);
	exit(1);
}

void rtrim(char *str){
	int n=strlen(str);
	while((--n>0)&&(str[n]==' ' || str[n]=='\n'));
	str[n+1]='\0';
}

int main(int argc, char *argv[]) {
	if (!getuid()) end("root doesn't need this helper");

	if (argc != 2) end("bad argument");

	struct passwd *pw;
	pw = getpwuid(getuid());
	if (!pw) end("user doesn't exist");

	char users[LOGIN_NAME_MAX];
	char crontab[sizeof CRONTAB_DIR + 1 + LOGIN_NAME_MAX];
	char temp[sizeof crontab + 7];

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
			while(fgets(buffer, sizeof(buffer), file)) {
				printf("%s", buffer);
			}
			fclose(file);
			break;
		case 'w':
			file = fopen("/etc/cron.allow", "r");
			if (file != NULL) {
				int allowed=0;
				while(fgets(users, sizeof(users), file)) {
					rtrim(users);
					if (!strcmp(pw->pw_name, users)) {
						allowed=1;
						break;
					}
				}
				fclose(file);
				if (!allowed) end("you're not in /etc/cron.allow");
			} else  {
				file = fopen("/etc/cron.deny", "r");
				if (file != NULL) {
					while(fgets(users, sizeof(users), file)) {
						rtrim(users);
						if (!strcmp(pw->pw_name, users)) {
							fclose(file);
							end("you are in /etc/cron.deny");
						}
					}
					fclose(file);
				} else end("without /etc/cron.allow or /etc/cron.deny; only root can install crontabs");
			}

			snprintf(temp, sizeof temp, "%s.XXXXXX", crontab);
			int fd = mkstemp(temp);
			file = fdopen(fd, "w");
			if (file == NULL) {
				perror("Cannot open output file");
				return 1;
			}
			int lines=0;
			while(fgets(buffer, sizeof(buffer), stdin)) {
				lines++;
				if (fprintf(file, "%s", buffer) < 0) {
					perror("Cannot write to file");
					fclose(file);
					unlink(temp);
					return 1;
				};
				if (lines > MAX_LINES) {
					fclose(file);
					unlink(temp);
					end("maximum lines reached");
				}
			}
			if (fclose(file)) {perror("fclose"); return 1;}
			struct group *grp;
			grp = getgrnam("crontab");
			fchown(fd,getuid(),grp->gr_gid);
			fchmod(fd,0600);
			if (rename(temp,crontab)) {perror("rename"); return 1;}
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
