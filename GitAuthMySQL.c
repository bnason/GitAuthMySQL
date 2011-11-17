#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <stdarg.h>
#include <pcre.h>
#include <mysql/mysql.h>

int mysqlRepoPermissionsGet(char* repo, char* owner, char* user);
void regexCommandPathGet(const char* sshOriginalCommand, char** gitRepo, char** gitOwner);
void regexRepoOwnerGet(const char* sshOriginalCommand, char** gitRepo, char** gitOwner);
void die(int ret, char* format, ...);
void log(char* format, ...);
void vlog(char* format, va_list args);
char* sprintfa(char* format, ...);
char* vsprintfa(char* format, va_list args);

#define OVECCOUNT 30

int main(int argc, char* argv[])
{
	char* sshUser = getenv("USER");
	char* sshOriginalCommand = getenv("SSH_ORIGINAL_COMMAND");
	char* gitUser = getenv("GIT_USER");
	char* gitCommand = NULL;
	char* gitRepo = NULL;
	char* gitRepoPath = NULL;
	char* gitRepoOwner = NULL;

	if (gitUser == NULL)
		die(1, "No git user!");

	if (sshOriginalCommand == NULL)
		die(1, "No command given");

	regexCommandPathGet(sshOriginalCommand, &gitCommand, &gitRepoPath);
	regexRepoOwnerGet(gitRepoPath, &gitRepo, &gitRepoOwner);

	int gitRepoPermissions = mysqlRepoPermissionsGet(gitRepo, gitRepoOwner, gitUser);

	log("User = %s, Command = %s, Path = %s, Repo = %s, Owner = %s, Permissions Granted = %d", gitUser, gitCommand, gitRepoPath, gitRepo, gitRepoOwner, gitRepoPermissions);
	
	if (!(gitRepoPermissions & 0b10))
		die(1, "You do not have permissions to write\n");

	char* command = sprintfa("git shell -c \"%s '%s'\"", gitCommand, gitRepoPath);

	system(command);

	return 0;
}

void regexRepoOwnerGet(const char* sshOriginalCommand, char** gitRepo, char** gitRepoOwner)
{
	pcre* re;
	const char* error;
	int erroroffset;
	int ovector[OVECCOUNT];
	int rc, i;

	re = pcre_compile("([^/]+)/([^/]+)$", 0, &error, &erroroffset, NULL);

	if (re == NULL)
	{
		log("PCRE compilatio failed at offset %d: %s", erroroffset, error);
	}

	rc = pcre_exec(re, NULL, sshOriginalCommand, strlen(sshOriginalCommand), 0, 0, ovector, OVECCOUNT);

	if (rc < 0)
	{
		switch(rc)
		{
			case PCRE_ERROR_NOMATCH:
				log("No match"); break;
			default:
				log("Matching error %d", rc); break;
		}
		return;
	}

//	log("Match succeeded");

	if (rc == 0)
	{
		rc = OVECCOUNT/3;
		log("ovector only has room for %d captured substrings", rc - 1);
	}

	if (rc == 3)
	{
		*gitRepoOwner = sprintfa("%.*s", ovector[2*1+1] - ovector[2*1], sshOriginalCommand + ovector[2*1]);
		*gitRepo = sprintfa("%.*s", ovector[2*2+1] - ovector[2*2], sshOriginalCommand + ovector[2*2]);
	}
}

void regexCommandPathGet(const char* sshOriginalCommand, char** gitCommand, char** gitRepoPath)
{
	pcre* re;
	const char* error;
	int erroroffset;
	int ovector[OVECCOUNT];
	int rc, i;

	re = pcre_compile("(git\\-[^ ]*) '(.*)'", 0, &error, &erroroffset, NULL);

	if (re == NULL)
	{
		log("PCRE compilatio failed at offset %d: %s", erroroffset, error);
	}

	rc = pcre_exec(re, NULL, sshOriginalCommand, strlen(sshOriginalCommand), 0, 0, ovector, OVECCOUNT);

	if (rc < 0)
	{
		switch(rc)
		{
			case PCRE_ERROR_NOMATCH:
				log("No match"); break;
			default:
				log("Matching error %d", rc); break;
		}
		return;
	}

//	log("Match succeeded");

	if (rc == 0)
	{
		rc = OVECCOUNT/3;
		log("ovector only has room for %d captured substrings", rc - 1);
	}

	if (rc == 3)
	{
		*gitCommand = sprintfa("%.*s", ovector[2*1+1] - ovector[2*1], sshOriginalCommand + ovector[2*1]);
		*gitRepoPath = sprintfa("%.*s", ovector[2*2+1] - ovector[2*2], sshOriginalCommand + ovector[2*2]);
	}

/*
	for (i = 0; i < rc; i++)
	{
		char* substring_start = sshOriginalCommand + ovector[2*i];
		int substring_length = ovector[2*i+1] - ovector[2*i];
		log("%2d: %.*s", i, substring_length, substring_start);
	}
*/
}

int mysqlRepoPermissionsGet(char* repo, char* owner, char* user)
{
	MYSQL *conn;
	MYSQL *conn2;
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_fields;
	int i;
	int ret;

	conn = mysql_init(NULL);
	if (mysql_real_connect(conn, "127.0.0.1", "git", "F7DcyxaKMm3MMn8c", "git", 0, NULL, 0) == NULL)
		log("MySQL connection error: %s", mysql_error(conn));
	else
	{
		char* query = sprintfa("SELECT users.username, user_repository.UID, user_repository.RID, user_repository.Read, \ user_repository.Write \
FROM user_repository \
INNER JOIN users ON users.username = \"%s\" AND users.ID = user_repository.UID \
INNER JOIN repositories ON repositories.name = \"%s\" AND repositories.UID = ( \
	SELECT UID \
	FROM repositories \
	INNER JOIN users ON users.username = \"%s\" AND users.ID = repositories.UID \
)", user, repo, owner);

		ret = mysql_query(conn, query);
		free(query);
		if (ret != 0)
			log("MySQL query error: %s", mysql_error(conn));
		ret = 0;

		result = mysql_store_result(conn);

		if (result != NULL)
		{
			num_fields = mysql_num_fields(result);

			while ((row = mysql_fetch_row(result)))
			{
				ret = (atoi(row[3]) * 1) + (atoi(row[4]) * 2);
			}

			mysql_free_result(result);
		}

		mysql_close(conn);
	}
	return ret;
}

void die(int ret, char* format, ...)
{
	va_list args;
	va_start(args, format);

	vfprintf(stderr, format, args);
	vlog(format, args);

	va_end(args);

	exit(ret);
}

void log(char* format, ...)
{
	va_list args;
	va_start(args, format);
	vlog(format, args);
	va_end(args);
}

void vlog(char* format, va_list args)
{
	char* message = vsprintfa(format, args);

	time_t ltime = time(NULL);
	
	char* timestamp = strdup(asctime(localtime(&ltime)));

	timestamp[strlen(timestamp)-2] = '\0';

	char* echo = sprintfa("echo \"%s : %s\" >> /home/git/sshlog", timestamp,  message);

	system(echo);

	free(echo);
	free(message);
}

char* sprintfa(char* format, ...)
{
	va_list args;
	va_start(args, format);
	return vsprintfa(format, args);
	va_end(args);
}

char* vsprintfa(char* format, va_list args)
{
	int numBytes = sizeof('\0') + vsnprintf(NULL, 0, format, args);

	char* str = (char*) malloc(sizeof(char) * numBytes);
	if (str == NULL)
		return NULL;

	vsprintf(str, format, args);
	return (str);
}
