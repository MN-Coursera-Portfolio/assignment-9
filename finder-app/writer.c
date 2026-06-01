#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
    openlog("writer", LOG_PID, LOG_USER);
    if (argc < 3) {
        syslog(LOG_ERR, "Error: Missing arguments");
        closelog();
        return 1;
    }
    char *writefile = argv[1];
    char *writestr = argv[2];
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
    FILE *fp = fopen(writefile, "w");
    if (!fp) {
        syslog(LOG_ERR, "Error: Could not open file");
        closelog();
        return 1;
    }
    fputs(writestr, fp);
    fclose(fp);
    closelog();
    return 0;
}
