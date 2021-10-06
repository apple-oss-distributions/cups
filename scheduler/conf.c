/*
 * "$Id: conf.c,v 1.45.2.1 2006/12/05 22:11:09 jlovell Exp $"
 *
 *   Configuration routines for the Common UNIX Printing System (CUPS).
 *
 *   Copyright 1997-2005 by Easy Software Products, all rights reserved.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Easy Software Products and are protected by Federal
 *   copyright law.  Distribution and use rights are outlined in the file
 *   "LICENSE.txt" which should have been included with this file.  If this
 *   file is missing or damaged please contact Easy Software Products
 *   at:
 *
 *       Attn: CUPS Licensing Information
 *       Easy Software Products
 *       44141 Airport View Drive, Suite 204
 *       Hollywood, Maryland 20636 USA
 *
 *       Voice: (301) 373-9600
 *       EMail: cups-info@cups.org
 *         WWW: http://www.cups.org
 *
 * Contents:
 *
 *   ReadConfiguration()  - Read the cupsd.conf file.
 *   read_configuration() - Read a configuration file.
 *   read_location()      - Read a <Location path> definition.
 *   get_address()        - Get an address + port number from a line.
 *   conf_file_check()    - Fix the mode and ownership of a file or directory. 
 */

/*
 * Include necessary headers...
 */

#include "cupsd.h"
#include <stdarg.h>
#include <pwd.h>
#include <grp.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_DOMAINSOCKETS
#  include <sys/un.h>
#endif /* HAVE_DOMAINSOCKETS */

#ifdef HAVE_CDSASSL
#  include <Security/SecureTransport.h>
#  include <Security/SecIdentity.h>
#  include <Security/SecIdentitySearch.h>
#  include <Security/SecKeychain.h>
#endif /* HAVE_CDSASSL */

#ifdef HAVE_VSYSLOG
#  include <syslog.h>
#endif /* HAVE_VSYSLOG */


/*
 * Possibly missing network definitions...
 */

#ifndef INADDR_NONE
#  define INADDR_NONE	0xffffffff
#endif /* !INADDR_NONE */


/*
 * Configuration variable structure...
 */

typedef struct
{
  char	*name;		/* Name of variable */
  void	*ptr;		/* Pointer to variable */
  int	type;		/* Type (int, string, address) */
} var_t;

#define VAR_INTEGER	0
#define VAR_STRING	1
#define VAR_BOOLEAN	2


/*
 * Local globals...
 */

static var_t	variables[] =
{
  { "AccessLog",		&AccessLog,		VAR_STRING },
  { "AutoPurgeJobs", 		&JobAutoPurge,		VAR_BOOLEAN },
  { "BrowseInterval",		&BrowseInterval,	VAR_INTEGER },
  { "BrowsePort",		&BrowsePort,		VAR_INTEGER },
  { "BrowseShortNames",		&BrowseShortNames,	VAR_BOOLEAN },
  { "BrowseTimeout",		&BrowseTimeout,		VAR_INTEGER },
  { "Browsing",			&Browsing,		VAR_BOOLEAN },
  { "Classification",		&Classification,	VAR_STRING },
  { "ClassifyOverride",		&ClassifyOverride,	VAR_BOOLEAN },
  { "ConfigFilePerm",		&ConfigFilePerm,	VAR_INTEGER },
  { "DataDir",			&DataDir,		VAR_STRING },
  { "DefaultCharset",		&DefaultCharset,	VAR_STRING },
  { "DefaultLanguage",		&DefaultLanguage,	VAR_STRING },
  { "DefaultShared", 		&DefaultShared,		VAR_BOOLEAN },
  { "DocumentRoot",		&DocumentRoot,		VAR_STRING },
  { "ErrorLog",			&ErrorLog,		VAR_STRING },
  { "FaxRetryLimit",		&FaxRetryLimit,		VAR_INTEGER },
  { "FaxRetryInterval",		&FaxRetryInterval,	VAR_INTEGER },
  { "FileDevice",		&FileDevice,		VAR_BOOLEAN },
  { "FilterLimit",		&FilterLimit,		VAR_INTEGER },
  { "FilterNice",		&FilterNice,		VAR_INTEGER },
  { "FontPath",			&FontPath,		VAR_STRING },
  { "HideImplicitMembers",	&HideImplicitMembers,	VAR_BOOLEAN },
  { "ImplicitClasses",		&ImplicitClasses,	VAR_BOOLEAN },
  { "ImplicitAnyClasses",	&ImplicitAnyClasses,	VAR_BOOLEAN },
  { "KeepAliveTimeout",		&KeepAliveTimeout,	VAR_INTEGER },
  { "KeepAlive",		&KeepAlive,		VAR_BOOLEAN },
  { "LimitRequestBody",		&MaxRequestSize,	VAR_INTEGER },
  { "ListenBackLog",		&ListenBackLog,		VAR_INTEGER },
  { "LogFilePerm",		&LogFilePerm,		VAR_INTEGER },
  { "MaxClients",		&MaxClients,		VAR_INTEGER },
  { "MaxClientsPerHost",	&MaxClientsPerHost,	VAR_INTEGER },
  { "MaxCopies",		&MaxCopies,		VAR_INTEGER },
#ifdef __APPLE__
  { "MinCopies",		&MinCopies,		VAR_INTEGER },
  { "AppleQuotas",		&AppleQuotas,		VAR_BOOLEAN },
  { "ApplePreserveJobHistoryAttributes",
				&ApplePreserveJobHistoryAttributes,
				VAR_BOOLEAN },
#endif  /* __APPLE__ */
  { "MaxJobs",			&MaxJobs,		VAR_INTEGER },
  { "MaxJobsPerPrinter",	&MaxJobsPerPrinter,	VAR_INTEGER },
  { "MaxJobsPerUser",		&MaxJobsPerUser,	VAR_INTEGER },
  { "MaxLogSize",		&MaxLogSize,		VAR_INTEGER },
  { "MaxPrinterHistory",	&MaxPrinterHistory,	VAR_INTEGER },
  { "MaxRequestSize",		&MaxRequestSize,	VAR_INTEGER },
  { "PageLog",			&PageLog,		VAR_STRING },
  { "PreserveJobFiles",		&JobFiles,		VAR_BOOLEAN },
  { "PreserveJobHistory",	&JobHistory,		VAR_BOOLEAN },
  { "Printcap",			&Printcap,		VAR_STRING },
  { "PrintcapGUI",		&PrintcapGUI,		VAR_STRING },
  { "ReloadTimeout",		&ReloadTimeout,		VAR_INTEGER },
  { "RemoteRoot",		&RemoteRoot,		VAR_STRING },
  { "RequestRoot",		&RequestRoot,		VAR_STRING },
  { "RIPCache",			&RIPCache,		VAR_STRING },
  { "RunAsUser", 		&RunAsUser,		VAR_BOOLEAN },
  { "RootCertDuration",		&RootCertDuration,	VAR_INTEGER },
  { "ServerAdmin",		&ServerAdmin,		VAR_STRING },
  { "ServerBin",		&ServerBin,		VAR_STRING },
#ifdef HAVE_SSL
  { "ServerCertificate",	&ServerCertificate,	VAR_STRING },
#  if defined(HAVE_LIBSSL) || defined(HAVE_GNUTLS)
  { "ServerKey",		&ServerKey,		VAR_STRING },
#  endif /* HAVE_LIBSSL || HAVE_GNUTLS */
#  ifdef HAVE_CDSASSL
  { "SSLVerifyCertificates",	&SSLVerifyCertificates,	VAR_BOOLEAN },
#  endif /* HAVE_CDSASSL */
#endif /* HAVE_SSL */
  { "ServerName",		&ServerName,		VAR_STRING },
  { "ServerRoot",		&ServerRoot,		VAR_STRING },
  { "TempDir",			&TempDir,		VAR_STRING },
  { "Timeout",			&Timeout,		VAR_INTEGER }
};
#define NUM_VARS	(sizeof(variables) / sizeof(variables[0]))


/*
 * Local functions...
 */

static int	read_configuration(cups_file_t *fp);
static int	read_location(cups_file_t *fp, char *name, int linenum);
static int	get_address(char *value, unsigned defaddress, int defport,
		            struct sockaddr_in *address);
static int	conf_file_check(const char*filename, const char *root, int mode, 
			    int user, int group, int is_dir, int create_dir);


/*
 * 'ReadConfiguration()' - Read the cupsd.conf file.
 */

int					/* O - 1 on success, 0 otherwise */
ReadConfiguration(void)
{
  int		i;			/* Looping var */
  cups_file_t	*fp;			/* Configuration file */
  int		status;			/* Return status */
  char		temp[1024],		/* Temporary buffer */
		*slash;			/* Directory separator */
  char		type[MIME_MAX_SUPER + MIME_MAX_TYPE];
					/* MIME type name */
  char		*language;		/* Language string */
  struct passwd	*user;			/* Default user */
  struct group	*group;			/* Default group */
  char		*old_serverroot,	/* Old ServerRoot */
		*old_requestroot;	/* Old RequestRoot */


 /*
  * Shutdown the server...
  */

  StopServer();

 /*
  * Save the old root paths...
  */

  old_serverroot = NULL;
  SetString(&old_serverroot, ServerRoot);
  old_requestroot = NULL;
  SetString(&old_requestroot, RequestRoot);

 /*
  * Reset the server configuration data...
  */

  DeleteAllLocations();

  if (NumBrowsers > 0)
  {
    free(Browsers);

    NumBrowsers = 0;
  }

  if (NumPolled > 0)
  {
    free(Polled);

    NumPolled = 0;
  }

  if (NumRelays > 0)
  {
    for (i = 0; i < NumRelays; i ++)
      if (Relays[i].from.type == AUTH_NAME)
	free(Relays[i].from.mask.name.name);

    free(Relays);

    NumRelays = 0;
  }

  if (NumListeners > 0)
  {
#ifdef HAVE_DOMAINSOCKETS
  int i;			/* Looping var */
  listener_t	*lis;		/* Current listening socket */

  for (i = NumListeners, lis = Listeners; i > 0; i --, lis ++)
    if (lis->address.sin_family == AF_LOCAL)
      ClearString((char **)&lis->address.sin_addr);
#endif /* HAVE_DOMAINSOCKETS */

    free(Listeners);

    NumListeners = 0;
  }

 /*
  * String options...
  */

  gethostname(temp, sizeof(temp));
  SetString(&ServerName, temp);
  SetStringf(&ServerAdmin, "root@%s", temp);
  SetString(&ServerBin, CUPS_SERVERBIN);
  SetString(&RequestRoot, CUPS_REQUESTS);
  SetString(&DocumentRoot, CUPS_DOCROOT);
  SetString(&DataDir, CUPS_DATADIR);
  SetString(&AccessLog, CUPS_LOGDIR "/access_log");
  SetString(&ErrorLog, CUPS_LOGDIR "/error_log");
  SetString(&PageLog, CUPS_LOGDIR "/page_log");
  SetString(&Printcap, "/etc/printcap");
  SetString(&PrintcapGUI, "/usr/bin/glpoptions");
  SetString(&FontPath, CUPS_FONTPATH);
  SetString(&RemoteRoot, "remroot");
  SetString(&ServerHeader, "CUPS/1.1");

  strlcpy(temp, ConfigurationFile, sizeof(temp));
  if ((slash = strrchr(temp, '/')) != NULL)
    *slash = '\0';

  SetString(&ServerRoot, temp);

  ClearString(&Classification);
  ClassifyOverride  = 0;

#ifdef HAVE_SSL
#  ifdef HAVE_CDSASSL
  if (ServerCertificatesArray)
  {
    CFRelease(ServerCertificatesArray);
    ServerCertificatesArray = NULL;
  }
  SetString(&ServerCertificate, "/Library/Keychains/System.keychain");
#  else
  SetString(&ServerCertificate, "ssl/server.crt");
  SetString(&ServerKey, "ssl/server.key");
#  endif /* HAVE_CDSASSL */
#endif /* HAVE_SSL */

  if ((language = DEFAULT_LANGUAGE) == NULL)
    language = "en";
  else if (strcmp(language, "C") == 0 || strcmp(language, "POSIX") == 0)
    language = "en";

  SetString(&DefaultLanguage, language);
  SetString(&DefaultCharset, DEFAULT_CHARSET);

  SetString(&RIPCache, "8m");

  if (getenv("TMPDIR") == NULL)
    SetString(&TempDir, CUPS_REQUESTS "/tmp");
  else
    SetString(&TempDir, getenv("TMPDIR"));

 /*
  * Find the default system group: "sys", "system", or "root"...
  */

  group = getgrnam(CUPS_DEFAULT_GROUP);
  endgrent();

  NumSystemGroups = 0;

  if (group != NULL)
  {
    SetString(&SystemGroups[0], CUPS_DEFAULT_GROUP);
    Group = group->gr_gid;
  }
  else
  {
    group = getgrgid(0);
    endgrent();

    if (group != NULL)
    {
      SetString(&SystemGroups[0], group->gr_name);
      Group = 0;
    }
    else
    {
      SetString(&SystemGroups[0], "unknown");
      Group = 0;
    }
  }

 /*
  * Find the default user...
  */

  if ((user = getpwnam(CUPS_DEFAULT_USER)) == NULL)
    User = 1;	/* Force to a non-priviledged account */
  else
    User = user->pw_uid;

  endpwent();

 /*
  * Numeric options...
  */

  ConfigFilePerm      = 0640;
  LogFilePerm         = 0644;

  FaxRetryLimit       = 5;
  FaxRetryInterval    = 300;
  FileDevice          = FALSE;
  FilterLevel         = 0;
  FilterLimit         = 0;
  FilterNice          = 0;
  HostNameLookups     = FALSE;
  ImplicitClasses     = TRUE;
  ImplicitAnyClasses  = FALSE;
  HideImplicitMembers = TRUE;
  KeepAlive           = TRUE;
  KeepAliveTimeout    = DEFAULT_KEEPALIVE;
  ListenBackLog       = SOMAXCONN;
  LogLevel            = L_ERROR;
  MaxClients          = 100;
  MaxClientsPerHost   = 0;
  MaxLogSize          = 1024 * 1024;
  MaxPrinterHistory   = 10;
  MaxRequestSize      = 0;
  ReloadTimeout	      = 60;
  RootCertDuration    = 300;
  RunAsUser           = FALSE;
  Timeout             = DEFAULT_TIMEOUT;

  BrowseInterval      = DEFAULT_INTERVAL;
  BrowsePort          = ippPort();

#ifdef HAVE_DNSSD
  BrowseLocalProtocols = BROWSE_CUPS | BROWSE_DNSSD;
#else
  BrowseLocalProtocols = BROWSE_CUPS;
#endif

  BrowseRemoteProtocols= BROWSE_CUPS;

  BrowseShortNames    = TRUE;
  BrowseTimeout       = DEFAULT_TIMEOUT;
  Browsing            = TRUE;

  JobHistory          = DEFAULT_HISTORY;
  JobFiles            = DEFAULT_FILES;
  JobAutoPurge        = 0;
  MaxJobs             = 500;
  MaxJobsPerUser      = 0;
  MaxJobsPerPrinter   = 0;
  MaxCopies           = 100;
#ifdef __APPLE__
  MinCopies           = 1;
  AppleQuotas         = TRUE;
  ApplePreserveJobHistoryAttributes 
		      = FALSE;
#endif  /* __APPLE__ */
#ifdef HAVE_CDSASSL
  SSLVerifyCertificates = FALSE;
#endif /* HAVE_CDSASSL */

 /*
  * Read the configuration file...
  */

  if ((fp = cupsFileOpen(ConfigurationFile, "r")) == NULL)
    return (0);

  status = read_configuration(fp);

  cupsFileClose(fp);

  if (!status)
    return (0);

  if (RunAsUser)
    RunUser = User;
  else
    RunUser = getuid();

 /*
  * Use the default system group if none was supplied in cupsd.conf...
  */

  if (NumSystemGroups == 0)
    NumSystemGroups ++;

 /*
  * Get the access control list for browsing...
  */

  BrowseACL = FindLocation("CUPS_INTERNAL_BROWSE_ACL");

 /*
  * Open the system log for cupsd if necessary...
  */

#ifdef HAVE_VSYSLOG
  if (strcmp(AccessLog, "syslog") == 0 ||
      strcmp(ErrorLog, "syslog") == 0 ||
      strcmp(PageLog, "syslog") == 0)
    openlog("cupsd", LOG_PID | LOG_NOWAIT | LOG_NDELAY, LOG_LPR);
#endif /* HAVE_VSYSLOG */

 /*
  * Log the configuration file that was used...
  */

  LogMessage(L_INFO, "Loaded configuration file \"%s\"", ConfigurationFile);

 /*
  * Check that we have at least one listen/port line; if not, report this
  * as an error and exit!
  */

  if (NumListeners == 0)
  {
   /*
    * No listeners!
    */

    LogMessage(L_EMERG, "No valid Listen or Port lines were found in the configuration file!");

   /*
    * Commit suicide...
    */

    kill(getpid(), SIGTERM);
  }

 /*
  * Set the default locale using the language and charset...
  */

  SetStringf(&DefaultLocale, "%s.%s", DefaultLanguage, DefaultCharset);

 /*
  * Update all relative filenames to include the full path from ServerRoot...
  */

  if (DocumentRoot[0] != '/')
    SetStringf(&DocumentRoot, "%s/%s", ServerRoot, DocumentRoot);

  if (RequestRoot[0] != '/')
    SetStringf(&RequestRoot, "%s/%s", ServerRoot, RequestRoot);

  if (ServerBin[0] != '/')
    SetStringf(&ServerBin, "%s/%s", ServerRoot, ServerBin);

#ifdef HAVE_SSL
  if (ServerCertificate[0] != '/')
    SetStringf(&ServerCertificate, "%s/%s", ServerRoot, ServerCertificate);

#  if defined(HAVE_LIBSSL) || defined(HAVE_GNUTLS)
  if (ServerKey[0] != '/')
    SetStringf(&ServerKey, "%s/%s", ServerRoot, ServerKey);
#  endif /* HAVE_LIBSSL || HAVE_GNUTLS */
#endif /* HAVE_SSL */

 /*
  * Make sure that ServerRoot and the config files are owned and
  * writable by the user and group in the cupsd.conf file...
  */

  conf_file_check(ServerRoot, NULL   , 0755, RunUser, Group, 1, 0);
  conf_file_check("certs", ServerRoot, 0711, RunUser, Group, 1, 0);
  conf_file_check("ppd"  , ServerRoot, 0755, RunUser, Group, 1, 0);

  conf_file_check("cupsd.conf"   , ServerRoot, ConfigFilePerm, RunUser, Group, 0, 0);
  conf_file_check("classes.conf" , ServerRoot, 0600, RunUser, Group, 0, 0);
  conf_file_check("printers.conf", ServerRoot, 0600, RunUser, Group, 0, 0);
  conf_file_check("passwd.md5"   , ServerRoot, 0600, RunUser, Group, 0, 0);

#  if defined(HAVE_LIBSSL) || defined(HAVE_GNUTLS)
  conf_file_check("ssl"  , ServerRoot, 0700, RunUser, Group, 1, 0);
  conf_file_check(ServerCertificate, NULL, ConfigFilePerm, RunUser, Group, 0, 0);
  conf_file_check(ServerKey, NULL, ConfigFilePerm, RunUser, Group, 0, 0);
#  endif /* HAVE_LIBSSL || HAVE_GNUTLS */

 /*
  * Make sure the request and temporary directories have the right
  * permissions...
  */

  conf_file_check(RequestRoot, NULL, 0710, RunUser, Group, 1, 0);

  if (strncmp(TempDir, RequestRoot, strlen(RequestRoot)) == 0)
  {
   /*
    * Only update ownership and permissions if the CUPS temp directory
    * is under the spool directory...
    */

    conf_file_check(TempDir, NULL, 01770, RunUser, Group, 1, 1);

   /*
    * Clean out the temporary directory...
    */

    DIR			*dir;		/* Temporary directory */
    struct dirent	*dent;		/* Directory entry */
    char		tempfile[1024];	/* Temporary filename */
    struct stat		fileinfo;	/* File information */

    if ((dir = opendir(TempDir)) != NULL)
    {
      LogMessage(L_DEBUG, "Cleaning out old temporary files in \"%s\"...", 
      			 TempDir);

      while ((dent = readdir(dir)) != NULL)
      {
       /*
	* Skip "." and ".."...
	*/
    
	if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
	  continue;

        snprintf(tempfile, sizeof(tempfile), "%s/%s", TempDir, dent->d_name);

	if (lstat(tempfile, &fileinfo))
	{
	  LogMessage(L_WARN, "ReadConfiguration: Unable to stat \"%s\": %s",
		     dent->d_name, strerror(errno));
	  continue;
	}

	if (S_ISDIR(fileinfo.st_mode))
	  continue;

	if (unlink(tempfile))
	  LogMessage(L_ERROR,
	                  "Unable to remove temporary file \"%s\" - %s",
	                  tempfile, strerror(errno));
        else
	  LogMessage(L_DEBUG, "Removed temporary file \"%s\"...",
	                  tempfile);
      }

      closedir(dir);
    }
    else
      LogMessage(L_ERROR,
                      "Unable to open temporary directory \"%s\" - %s",
                      TempDir, strerror(errno));
  }

 /*
  * Check the MaxClients setting, and then allocate memory for it...
  */

  if (MaxClients > (MaxFDs / 3) || MaxClients <= 0)
  {
    if (MaxClients > 0)
      LogMessage(L_INFO, "MaxClients limited to 1/3 of the file descriptor limit (%d)...",
                 MaxFDs);

    MaxClients = MaxFDs / 3;
  }

  LogMessage(L_INFO, "Configured for up to %d clients.", MaxClients);

  if (Classification && strcasecmp(Classification, "none") == 0)
    ClearString(&Classification);

  if (Classification)
    LogMessage(L_INFO, "Security set to \"%s\"", Classification);

 /*
  * Update the MaxClientsPerHost value, as needed...
  */

  if (MaxClientsPerHost <= 0)
    MaxClientsPerHost = MaxClients;

  if (MaxClientsPerHost > MaxClients)
    MaxClientsPerHost = MaxClients;

  LogMessage(L_INFO, "Allowing up to %d client connections per host.",
             MaxClientsPerHost);

 /*
  * If we are doing a full reload or the server root has changed, flush
  * the jobs, printers, etc. and start from scratch...
  */

  if (NeedReload == RELOAD_ALL ||
      !old_serverroot || !ServerRoot || strcmp(old_serverroot, ServerRoot) ||
      !old_requestroot || !RequestRoot || strcmp(old_requestroot, RequestRoot))
  {
    LogMessage(L_INFO, "Full reload is required.");

   /*
    * Free all memory...
    */

    FreeAllJobs();
    DeleteAllClasses();
    DeleteAllPrinters();

    DefaultPrinter = NULL;

    if (Devices)
    {
      ippDelete(Devices);
      Devices = NULL;
      BackendsExeced = 0;
    }

    if (PPDs)
    {
      ippDelete(PPDs);
      PPDs = NULL;
    }

    if (MimeDatabase != NULL)
      mimeDelete(MimeDatabase);

    if (NumMimeTypes)
    {
      for (i = 0; i < NumMimeTypes; i ++)
	free((void *)MimeTypes[i]);

      free(MimeTypes);
    }

   /*
    * Read the MIME type and conversion database...
    */

    snprintf(temp, sizeof(temp), "%s/filter", ServerBin);

    MimeDatabase = mimeNew();
    mimeMerge(MimeDatabase, ServerRoot, temp);

   /*
    * Create a list of MIME types for the document-format-supported
    * attribute...
    */

    NumMimeTypes = MimeDatabase->num_types;
    if (!mimeType(MimeDatabase, "application", "octet-stream"))
      NumMimeTypes ++;

    MimeTypes = calloc(NumMimeTypes, sizeof(const char *));

    for (i = 0; i < MimeDatabase->num_types; i ++)
    {
      snprintf(type, sizeof(type), "%s/%s", MimeDatabase->types[i]->super,
               MimeDatabase->types[i]->type);

      MimeTypes[i] = strdup(type);
    }

    if (i < NumMimeTypes)
      MimeTypes[i] = strdup("application/octet-stream");

   /*
    * Load banners...
    */

    snprintf(temp, sizeof(temp), "%s/banners", DataDir);
    LoadBanners(temp);

   /*
    * Load printers and classes...
    */

    LoadAllPrinters();
    LoadAllClasses();

    CreateCommonData();

   /*
    * Load devices and PPDs...
    */

#ifdef __APPLE__
   /*
    * For a faster and leaner startup we load the complete device list 
    * and PPDs on demand rather than doing it here.
    */

    snprintf(temp, sizeof(temp), "%s/backend", ServerBin);
    LoadDevices(temp, FALSE);
#else
    snprintf(temp, sizeof(temp), "%s/backend", ServerBin);
    LoadDevices(temp, TRUE);

    snprintf(temp, sizeof(temp), "%s/model", DataDir);
    LoadPPDs(temp);
#endif /* __APPLE__ */

   /*
    * Load queued jobs...
    */

    LoadAllJobs(ACTIVE_JOBS);

   /*
    * Now that the printer list is stable write the printcap file...
    */

    WritePrintcap();

    LogMessage(L_INFO, "Full reload complete.");
  }
  else
  {
    CreateCommonData();

    LogMessage(L_INFO, "Partial reload complete.");
  }

 /*
  * Reset the reload state...
  */

  NeedReload = RELOAD_NONE;

  ClearString(&old_serverroot);
  ClearString(&old_requestroot);

 /*
  * Startup the server and return...
  */

  StartServer();

  return (1);
}


/*
 * 'read_configuration()' - Read a configuration file.
 */

static int				/* O - 1 on success, 0 on failure */
read_configuration(cups_file_t *fp)	/* I - File to read from */
{
  int		i;			/* Looping var */
  int		linenum;		/* Current line number */
  int		len;			/* Length of line */
  char		line[HTTP_MAX_BUFFER],	/* Line from file */
		name[256],		/* Parameter name */
		*nameptr,		/* Pointer into name */
		*value;			/* Pointer to value */
  int		valuelen;		/* Length of value */
  int		browseProtocol;		/* Browse protocol */
  var_t		*var;			/* Current variable */
  unsigned	address,		/* Address value */
		netmask;		/* Netmask value */
  int		ip[4],			/* IP address components */
		ipcount,		/* Number of components provided */
 		mask[4];		/* IP netmask components */
  dirsvc_relay_t *relay;		/* Relay data */
  dirsvc_poll_t	*poll;			/* Polling data */
  struct sockaddr_in polladdr;		/* Polling address */
  location_t	*location;		/* Browse location */
  cups_file_t	*incfile;		/* Include file */
  char		incname[1024];		/* Include filename */
  static unsigned netmasks[4] =		/* Standard netmasks... */
  {
    0xff000000,
    0xffff0000,
    0xffffff00,
    0xffffffff
  };


 /*
  * Loop through each line in the file...
  */

  linenum = 0;

  while (cupsFileGets(fp, line, sizeof(line)) != NULL)
  {
    linenum ++;

   /*
    * Skip comment lines...
    */

    if (line[0] == '#')
      continue;

   /*
    * Strip trailing whitespace, if any...
    */

    len = strlen(line);

    while (len > 0 && isspace(line[len - 1]))
    {
      len --;
      line[len] = '\0';
    }

   /*
    * Extract the name from the beginning of the line...
    */

    for (value = line; isspace(*value); value ++);

    for (nameptr = name; *value != '\0' && !isspace(*value) &&
                             nameptr < (name + sizeof(name) - 1);)
      *nameptr++ = *value++;
    *nameptr = '\0';

    while (isspace(*value))
      value ++;

    if (name[0] == '\0')
      continue;

   /*
    * Decode the directive...
    */

    if (strcasecmp(name, "Include") == 0)
    {
     /*
      * Include filename
      */

      if (value[0] == '/')
        strlcpy(incname, value, sizeof(incname));
      else
        snprintf(incname, sizeof(incname), "%s/%s", ServerRoot, value);

      if ((incfile = cupsFileOpen(incname, "rb")) == NULL)
        LogMessage(L_ERROR, "Unable to include config file \"%s\" - %s",
	           incname, strerror(errno));
      else
      {
        read_configuration(incfile);
	cupsFileClose(incfile);
      }
    }
    else if (strcasecmp(name, "<Location") == 0)
    {
     /*
      * <Location path>
      */

      if (line[len - 1] == '>')
      {
        line[len - 1] = '\0';

	linenum = read_location(fp, value, linenum);
	if (linenum == 0)
	  return (0);
      }
      else
      {
        LogMessage(L_ERROR, "Syntax error on line %d.",
	           linenum);
        return (0);
      }
    }
    else if (strcasecmp(name, "Port") == 0 ||
             strcasecmp(name, "Listen") == 0)
    {
     /*
      * Add a listening address to the list...
      */

      listener_t	*temp;		/* New listeners array */


      if (NumListeners == 0)
        temp = malloc(sizeof(listener_t));
      else
        temp = realloc(Listeners, (NumListeners + 1) * sizeof(listener_t));

      if (!temp)
      {
        LogMessage(L_ERROR, "Unable to allocate %s at line %d - %s.",
	           name, linenum, strerror(errno));
        continue;
      }

      Listeners = temp;
      temp      += NumListeners;

      memset(temp, 0, sizeof(listener_t));

      if (get_address(value, INADDR_ANY, IPP_PORT, &(temp->address)))
      {
        LogMessage(L_INFO, "Listening to %x:%d",
                   (unsigned)ntohl(temp->address.sin_addr.s_addr),
                   ntohs(temp->address.sin_port));
	NumListeners ++;
      }
      else
        LogMessage(L_ERROR, "Bad %s address %s at line %d.", name,
	           value, linenum);
    }
#ifdef HAVE_SSL
    else if (strcasecmp(name, "SSLPort") == 0 ||
             strcasecmp(name, "SSLListen") == 0)
    {
     /*
      * Add a listening address to the list...
      */

      listener_t	*temp;		/* New listeners array */


      if (NumListeners == 0)
        temp = malloc(sizeof(listener_t));
      else
        temp = realloc(Listeners, (NumListeners + 1) * sizeof(listener_t));

      if (!temp)
      {
        LogMessage(L_ERROR, "Unable to allocate %s at line %d - %s.",
	           name, linenum, strerror(errno));
        continue;
      }

      Listeners = temp;
      temp      += NumListeners;

      if (get_address(value, INADDR_ANY, IPP_PORT, &(temp->address)))
      {
        LogMessage(L_INFO, "Listening to %x:%d (SSL)",
                   (unsigned)ntohl(temp->address.sin_addr.s_addr),
                   ntohs(temp->address.sin_port));
        temp->encryption = HTTP_ENCRYPT_ALWAYS;
	NumListeners ++;
      }
      else
        LogMessage(L_ERROR, "Bad %s address %s at line %d.", name,
	           value, linenum);
    }
#endif /* HAVE_SSL */
    else if (strcasecmp(name, "BrowseAddress") == 0)
    {
     /*
      * Add a browse address to the list...
      */

      dirsvc_addr_t	*temp;		/* New browse address array */


      if (NumBrowsers == 0)
        temp = malloc(sizeof(dirsvc_addr_t));
      else
        temp = realloc(Browsers, (NumBrowsers + 1) * sizeof(dirsvc_addr_t));

      if (!temp)
      {
        LogMessage(L_ERROR, "Unable to allocate BrowseAddress at line %d - %s.",
	           linenum, strerror(errno));
        continue;
      }

      Browsers = temp;
      temp     += NumBrowsers;

      memset(temp, 0, sizeof(dirsvc_addr_t));

      if (strcasecmp(value, "@LOCAL") == 0)
      {
       /*
	* Send browse data to all local interfaces...
	*/

	strcpy(temp->iface, "*");
	NumBrowsers ++;
      }
      else if (strncasecmp(value, "@IF(", 4) == 0)
      {
       /*
	* Send browse data to the named interface...
	*/

	strlcpy(temp->iface, value + 4, sizeof(Browsers[0].iface));

        nameptr = temp->iface + strlen(temp->iface) - 1;
        if (*nameptr == ')')
	  *nameptr = '\0';

	NumBrowsers ++;
      }
      else if (get_address(value, INADDR_NONE, BrowsePort, &(temp->to)))
      {
        LogMessage(L_INFO, "Sending browsing info to %x:%d",
                   (unsigned)ntohl(temp->to.sin_addr.s_addr),
                   ntohs(temp->to.sin_port));

	NumBrowsers ++;
      }
      else
        LogMessage(L_ERROR, "Bad BrowseAddress %s at line %d.", value,
	           linenum);
    }
    else if (strcasecmp(name, "BrowseOrder") == 0)
    {
     /*
      * "BrowseOrder Deny,Allow" or "BrowseOrder Allow,Deny"...
      */

      if ((location = FindLocation("CUPS_INTERNAL_BROWSE_ACL")) == NULL)
        location = AddLocation("CUPS_INTERNAL_BROWSE_ACL");

      if (location == NULL)
        LogMessage(L_ERROR, "Unable to initialize browse access control list!");
      else if (strncasecmp(value, "deny", 4) == 0)
        location->order_type = AUTH_ALLOW;
      else if (strncasecmp(value, "allow", 5) == 0)
        location->order_type = AUTH_DENY;
      else
        LogMessage(L_ERROR, "Unknown BrowseOrder value %s on line %d.",
	           value, linenum);
    }
    else if (strcasecmp(name, "BrowseProtocols") == 0 ||
	     strcasecmp(name, "BrowseLocalProtocols") == 0 ||
	     strcasecmp(name, "BrowseRemoteProtocols") == 0)
    {
     /*
      * "BrowseProtocol name [... name]"
      * "BrowseLocalProtocols name [... name]"
      * "BrowseRemoteProtocols   name [... name]"
      */

      browseProtocol = 0;

      for (; *value;)
      {
        for (valuelen = 0; value[valuelen]; valuelen ++)
	  if (isspace(value[valuelen]) || value[valuelen] == ',')
	    break;

        if (value[valuelen])
        {
	  value[valuelen] = '\0';
	  valuelen ++;
	}

        if (strcasecmp(value, "cups") == 0)
	  browseProtocol |= BROWSE_CUPS;
        else if (strcasecmp(value, "slp") == 0)
	  browseProtocol |= BROWSE_SLP;
        else if (strcasecmp(value, "ldap") == 0)
	  browseProtocol |= BROWSE_LDAP;
#ifdef HAVE_DNSSD
        else if (strcasecmp(value, "dnssd") == 0 || 
        	 strcasecmp(value, "bonjour") == 0)
	  browseProtocol |= BROWSE_DNSSD;
#endif /* HAVE_DNSSD */
        else if (strcasecmp(value, "all") == 0)
	  browseProtocol |= BROWSE_ALL;
	else
	{
	  LogMessage(L_ERROR, "Unknown browse protocol \"%s\" on line %d.",
	             value, linenum);
          break;
	}

        for (value += valuelen; *value; value ++)
	  if (!isspace(*value) || *value != ',')
	    break;
      }

      if (strcasecmp(name, "BrowseProtocols") == 0)
	BrowseLocalProtocols = BrowseRemoteProtocols = browseProtocol;
      else if (strcasecmp(name, "BrowseLocalProtocols") == 0)
	BrowseLocalProtocols = browseProtocol;
      else
	BrowseRemoteProtocols = browseProtocol;
    }
    else if (strcasecmp(name, "BrowseAllow") == 0 ||
             strcasecmp(name, "BrowseDeny") == 0)
    {
     /*
      * BrowseAllow [From] host/ip...
      * BrowseDeny [From] host/ip...
      */

      if ((location = FindLocation("CUPS_INTERNAL_BROWSE_ACL")) == NULL)
        location = AddLocation("CUPS_INTERNAL_BROWSE_ACL");

      if (location == NULL)
        LogMessage(L_ERROR, "Unable to initialize browse access control list!");
      else
      {
	if (strncasecmp(value, "from ", 5) == 0)
	{
	 /*
          * Strip leading "from"...
	  */

	  value += 5;

	  while (isspace(*value))
	    value ++;
	}

       /*
	* Figure out what form the allow/deny address takes:
	*
	*    All
	*    None
	*    *.domain.com
	*    .domain.com
	*    host.domain.com
	*    nnn.*
	*    nnn.nnn.*
	*    nnn.nnn.nnn.*
	*    nnn.nnn.nnn.nnn
	*    nnn.nnn.nnn.nnn/mm
	*    nnn.nnn.nnn.nnn/mmm.mmm.mmm.mmm
	*/

	if (strcasecmp(value, "all") == 0)
	{
	 /*
          * All hosts...
	  */

          if (strcasecmp(name, "BrowseAllow") == 0)
	    AllowIP(location, 0, 0);
	  else
	    DenyIP(location, 0, 0);
	}
	else if (strcasecmp(value, "none") == 0)
	{
	 /*
          * No hosts...
	  */

          if (strcasecmp(name, "BrowseAllow") == 0)
	    AllowIP(location, ~0, 0);
	  else
	    DenyIP(location, ~0, 0);
	}
	else if (value[0] == '*' || value[0] == '.' || !isdigit(value[0]))
	{
	 /*
          * Host or domain name...
	  */

	  if (value[0] == '*')
	    value ++;

          if (strcasecmp(name, "BrowseAllow") == 0)
	    AllowHost(location, value);
	  else
	    DenyHost(location, value);
	}
	else
	{
	 /*
          * One of many IP address forms...
	  */

          memset(ip, 0, sizeof(ip));
          ipcount = sscanf(value, "%d.%d.%d.%d", ip + 0, ip + 1, ip + 2, ip + 3);
	  address = (((((ip[0] << 8) | ip[1]) << 8) | ip[2]) << 8) | ip[3];

          if ((value = strchr(value, '/')) != NULL)
	  {
	    value ++;
	    memset(mask, 0, sizeof(mask));
            switch (sscanf(value, "%d.%d.%d.%d", mask + 0, mask + 1,
	                   mask + 2, mask + 3))
	    {
	      case 1 :
	          netmask = (0xffffffff << (32 - mask[0])) & 0xffffffff;
	          break;
	      case 4 :
	          netmask = (((((mask[0] << 8) | mask[1]) << 8) |
		              mask[2]) << 8) | mask[3];
                  break;
	      default :
        	  LogMessage(L_ERROR, "Bad netmask value %s on line %d.",
	        	     value, linenum);
		  netmask = 0xffffffff;
		  break;
	    }
	  }
	  else
	    netmask = netmasks[ipcount - 1];

          if ((address & ~netmask) != 0)
	  {
	    LogMessage(L_WARN, "Discarding extra bits in %s address %08x for netmask %08x...",
	               name, address, netmask);
            address &= netmask;
	  }

          if (strcasecmp(name, "BrowseAllow") == 0)
	    AllowIP(location, address, netmask);
	  else
	    DenyIP(location, address, netmask);
	}
      }
    }
    else if (strcasecmp(name, "BrowseRelay") == 0)
    {
     /*
      * BrowseRelay [from] source [to] destination
      */

      if (NumRelays == 0)
        relay = malloc(sizeof(dirsvc_relay_t));
      else
        relay = realloc(Relays, (NumRelays + 1) * sizeof(dirsvc_relay_t));

      if (!relay)
      {
        LogMessage(L_ERROR, "Unable to allocate BrowseRelay at line %d - %s.",
	           linenum, strerror(errno));
        continue;
      }

      Relays = relay;
      relay  += NumRelays;

      memset(relay, 0, sizeof(dirsvc_relay_t));

      if (strncasecmp(value, "from ", 5) == 0)
      {
       /*
        * Strip leading "from"...
	*/

	value += 5;

	while (isspace(*value))
	  value ++;
      }

     /*
      * Figure out what form the from address takes:
      *
      *    *.domain.com
      *    .domain.com
      *    host.domain.com
      *    nnn.*
      *    nnn.nnn.*
      *    nnn.nnn.nnn.*
      *    nnn.nnn.nnn.nnn
      *    nnn.nnn.nnn.nnn/mm
      *    nnn.nnn.nnn.nnn/mmm.mmm.mmm.mmm
      */

      if (value[0] == '*' || value[0] == '.' || !isdigit(value[0]))
      {
       /*
        * Host or domain name...
	*/

	if (value[0] == '*')
	  value ++;

        strlcpy(name, value, sizeof(name));
	if ((nameptr = strchr(name, ' ')) != NULL)
	  *nameptr = '\0';

        relay->from.type             = AUTH_NAME;
	relay->from.mask.name.name   = strdup(name);
	relay->from.mask.name.length = strlen(name);
      }
      else
      {
       /*
        * One of many IP address forms...
	*/

        memset(ip, 0, sizeof(ip));
        ipcount = sscanf(value, "%d.%d.%d.%d", ip + 0, ip + 1, ip + 2, ip + 3);
	address = (((((ip[0] << 8) | ip[1]) << 8) | ip[2]) << 8) | ip[3];

        for (; *value; value ++)
	  if (*value == '/' || isspace(*value))
	    break;

        if (*value == '/')
	{
	  value ++;
	  memset(mask, 0, sizeof(mask));
          switch (sscanf(value, "%d.%d.%d.%d", mask + 0, mask + 1,
	                 mask + 2, mask + 3))
	  {
	    case 1 :
	        netmask = (0xffffffff << (32 - mask[0])) & 0xffffffff;
	        break;
	    case 4 :
	        netmask = (((((mask[0] << 8) | mask[1]) << 8) |
		            mask[2]) << 8) | mask[3];
                break;
	    default :
        	LogMessage(L_ERROR, "Bad netmask value %s on line %d.",
	        	   value, linenum);
		netmask = 0xffffffff;
		break;
	  }
	}
	else
	  netmask = netmasks[ipcount - 1];

        relay->from.type            = AUTH_IP;
	relay->from.mask.ip.address = address;
	relay->from.mask.ip.netmask = netmask;
      }

     /*
      * Skip value and trailing whitespace...
      */

      for (; *value; value ++)
	if (isspace(*value))
	  break;

      while (isspace(*value))
        value ++;

      if (strncasecmp(value, "to ", 3) == 0)
      {
       /*
        * Strip leading "to"...
	*/

	value += 3;

	while (isspace(*value))
	  value ++;
      }

     /*
      * Get "to" address and port...
      */

      if (get_address(value, INADDR_BROADCAST, BrowsePort, &(relay->to)))
      {
        if (relay->from.type == AUTH_NAME)
          LogMessage(L_INFO, "Relaying from %s to %x:%d",
	             relay->from.mask.name.name,
                     (unsigned)ntohl(relay->to.sin_addr.s_addr),
                     ntohs(relay->to.sin_port));
        else
          LogMessage(L_INFO, "Relaying from %x/%x to %x:%d",
                     relay->from.mask.ip.address, relay->from.mask.ip.netmask,
                     (unsigned)ntohl(relay->to.sin_addr.s_addr),
                     ntohs(relay->to.sin_port));

	NumRelays ++;
      }
      else
      {
        if (relay->from.type == AUTH_NAME)
	  free(relay->from.mask.name.name);

        LogMessage(L_ERROR, "Bad relay address %s at line %d.", value, linenum);
      }
    }
    else if (strcasecmp(name, "BrowsePoll") == 0)
    {
     /*
      * BrowsePoll address[:port]
      */

      if (NumPolled == 0)
        poll = malloc(sizeof(dirsvc_poll_t));
      else
        poll = realloc(Polled, (NumPolled + 1) * sizeof(dirsvc_poll_t));

      if (!poll)
      {
        LogMessage(L_ERROR, "Unable to allocate BrowsePoll at line %d - %s.",
	           linenum, strerror(errno));
        continue;
      }

      Polled = poll;
      poll   += NumPolled;

     /*
      * Get poll address and port...
      */

      if (get_address(value, INADDR_NONE, ippPort(), &polladdr))
      {
        LogMessage(L_INFO, "Polling %x:%d",
	           (unsigned)ntohl(polladdr.sin_addr.s_addr),
                   ntohs(polladdr.sin_port));

	NumPolled ++;
	memset(poll, 0, sizeof(dirsvc_poll_t));

        address = ntohl(polladdr.sin_addr.s_addr);

	sprintf(poll->hostname, "%d.%d.%d.%d", address >> 24,
	        (address >> 16) & 255, (address >> 8) & 255, address & 255);
        poll->port = ntohs(polladdr.sin_port);
      }
      else
        LogMessage(L_ERROR, "Bad poll address %s at line %d.", value, linenum);
    }
    else if (strcasecmp(name, "User") == 0)
    {
     /*
      * User ID to run as...
      */

      if (isdigit(value[0]))
        User = atoi(value);
      else
      {
        struct passwd *p;	/* Password information */

        endpwent();
	p = getpwnam(value);

	if (p != NULL)
	  User = p->pw_uid;
	else
	  LogMessage(L_WARN, "Unknown username \"%s\"",
	             value);
      }
    }
    else if (strcasecmp(name, "Group") == 0)
    {
     /*
      * Group ID to run as...
      */

      if (isdigit(value[0]))
        Group = atoi(value);
      else
      {
        struct group *g;	/* Group information */

        endgrent();
	g = getgrnam(value);

	if (g != NULL)
	  Group = g->gr_gid;
	else
	  LogMessage(L_WARN, "Unknown groupname \"%s\"",
	             value);
      }
    }
    else if (strcasecmp(name, "SystemGroup") == 0)
    {
     /*
      * System (admin) group(s)...
      */

      char	*valueptr,	/* Pointer into value */
		quote;		/* Quote character */


      for (i = NumSystemGroups; *value && i < MAX_SYSTEM_GROUPS; i ++)
      {
        if (*value == '\'' || *value == '\"')
	{
	 /*
	  * Scan quoted name...
	  */

	  quote = *value++;

	  for (valueptr = value; *valueptr; valueptr ++)
	    if (*valueptr == quote)
	      break;
	}
	else
	{
	 /*
	  * Scan space or comma-delimited name...
	  */

          for (valueptr = value; *valueptr; valueptr ++)
	    if (isspace(*valueptr) || *valueptr == ',')
	      break;
        }

        if (*valueptr)
          *valueptr++ = '\0';

        SetString(SystemGroups + i, value);

        value = valueptr;

        while (*value == ',' || isspace(*value))
	  value ++;
      }

      if (i)
        NumSystemGroups = i;
    }
    else if (strcasecmp(name, "HostNameLookups") == 0)
    {
     /*
      * Do hostname lookups?
      */

      if (strcasecmp(value, "off") == 0)
        HostNameLookups = 0;
      else if (strcasecmp(value, "on") == 0)
        HostNameLookups = 1;
      else if (strcasecmp(value, "double") == 0)
        HostNameLookups = 2;
      else
	LogMessage(L_WARN, "Unknown HostNameLookups %s on line %d.",
	           value, linenum);
    }
    else if (strcasecmp(name, "LogLevel") == 0)
    {
     /*
      * Amount of logging to do...
      */

      if (strcasecmp(value, "debug2") == 0)
        LogLevel = L_DEBUG2;
      else if (strcasecmp(value, "debug") == 0)
        LogLevel = L_DEBUG;
      else if (strcasecmp(value, "info") == 0)
        LogLevel = L_INFO;
      else if (strcasecmp(value, "notice") == 0)
        LogLevel = L_NOTICE;
      else if (strcasecmp(value, "warn") == 0)
        LogLevel = L_WARN;
      else if (strcasecmp(value, "error") == 0)
        LogLevel = L_ERROR;
      else if (strcasecmp(value, "crit") == 0)
        LogLevel = L_CRIT;
      else if (strcasecmp(value, "alert") == 0)
        LogLevel = L_ALERT;
      else if (strcasecmp(value, "emerg") == 0)
        LogLevel = L_EMERG;
      else if (strcasecmp(value, "none") == 0)
        LogLevel = L_NONE;
      else
        LogMessage(L_WARN, "Unknown LogLevel %s on line %d.", value, linenum);
    }
    else if (strcasecmp(name, "PrintcapFormat") == 0)
    {
     /*
      * Format of printcap file?
      */

      if (strcasecmp(value, "bsd") == 0)
        PrintcapFormat = PRINTCAP_BSD;
      else if (strcasecmp(value, "solaris") == 0)
        PrintcapFormat = PRINTCAP_SOLARIS;
      else
	LogMessage(L_WARN, "Unknown PrintcapFormat %s on line %d.",
	           value, linenum);
    }
    else if (!strcasecmp(name, "ServerTokens"))
    {
     /*
      * Set the string used for the Server header...
      */

      struct utsname plat;		/* Platform info */


      uname(&plat);

      if (!strcasecmp(value, "ProductOnly"))
        SetString(&ServerHeader, "CUPS");
      else if (!strcasecmp(value, "Major"))
        SetString(&ServerHeader, "CUPS/1");
      else if (!strcasecmp(value, "Minor"))
        SetString(&ServerHeader, "CUPS/1.1");
      else if (!strcasecmp(value, "Minimal"))
        SetString(&ServerHeader, CUPS_MINIMAL);
      else if (!strcasecmp(value, "OS"))
        SetStringf(&ServerHeader, CUPS_MINIMAL " (%s)", plat.sysname);
      else if (!strcasecmp(value, "Full"))
        SetStringf(&ServerHeader, CUPS_MINIMAL " (%s) IPP/1.1", plat.sysname);
      else if (!strcasecmp(value, "None"))
        ClearString(&ServerHeader);
      else
        LogMessage(L_WARN, "Unknown ServerTokens %s on line %d.", value, linenum);
    }
    else
    {
     /*
      * Find a simple variable in the list...
      */

      for (i = NUM_VARS, var = variables; i > 0; i --, var ++)
        if (strcasecmp(name, var->name) == 0)
	  break;

      if (i == 0)
      {
       /*
        * Unknown directive!  Output an error message and continue...
	*/

        LogMessage(L_ERROR, "Unknown directive %s on line %d.", name,
	           linenum);
        continue;
      }

      switch (var->type)
      {
        case VAR_INTEGER :
	    {
	      int	n;	/* Number */
	      char	*units;	/* Units */


              n = strtol(value, &units, 0);

	      if (units && *units)
	      {
        	if (tolower(units[0] & 255) == 'g')
		  n *= 1024 * 1024 * 1024;
        	else if (tolower(units[0] & 255) == 'm')
		  n *= 1024 * 1024;
		else if (tolower(units[0] & 255) == 'k')
		  n *= 1024;
		else if (tolower(units[0] & 255) == 't')
		  n *= 262144;
	      }

	      *((int *)var->ptr) = n;
	    }
	    break;

	case VAR_BOOLEAN :
	    if (strcasecmp(value, "true") == 0 ||
	        strcasecmp(value, "on") == 0 ||
		strcasecmp(value, "enabled") == 0 ||
		strcasecmp(value, "yes") == 0 ||
		atoi(value) != 0)
              *((int *)var->ptr) = TRUE;
	    else if (strcasecmp(value, "false") == 0 ||
	             strcasecmp(value, "off") == 0 ||
		     strcasecmp(value, "disabled") == 0 ||
		     strcasecmp(value, "no") == 0 ||
		     strcasecmp(value, "0") == 0)
              *((int *)var->ptr) = FALSE;
	    else
              LogMessage(L_ERROR, "Unknown boolean value %s on line %d.",
	                 value, linenum);
	    break;

	case VAR_STRING :
	    SetString((char **)var->ptr, value);
	    break;
      }
    }
  }

  return (1);
}


/*
 * 'read_location()' - Read a <Location path> definition.
 */

static int				/* O - New line number or 0 on error */
read_location(cups_file_t *fp,		/* I - Configuration file */
              char        *location,	/* I - Location name/path */
	      int         linenum)	/* I - Current line number */
{
  int		i;			/* Looping var */
  location_t	*loc,			/* New location */
		*parent;		/* Parent location */
  int		len;			/* Length of line */
  char		line[HTTP_MAX_BUFFER],	/* Line buffer */
		name[256],		/* Configuration directive */
		*nameptr,		/* Pointer into name */
		*value,			/* Value for directive */
		*valptr;		/* Pointer into value */
  unsigned	address,		/* Address value */
		netmask;		/* Netmask value */
  int		ip[4],			/* IP address components */
		ipcount,		/* Number of components provided */
 		mask[4];		/* IP netmask components */
  static unsigned	netmasks[4] =	/* Standard netmasks... */
  {
    0xff000000,
    0xffff0000,
    0xffffff00,
    0xffffffff
  };


  if ((parent = AddLocation(location)) == NULL)
    return (0);

  parent->limit = AUTH_LIMIT_ALL;
  loc           = parent;

  while (cupsFileGets(fp, line, sizeof(line)) != NULL)
  {
    linenum ++;

   /*
    * Skip comment lines...
    */

    if (line[0] == '#')
      continue;

   /*
    * Strip trailing whitespace, if any...
    */

    len = strlen(line);

    while (len > 0 && isspace(line[len - 1] & 255))
    {
      len --;
      line[len] = '\0';
    }

   /*
    * Extract the name from the beginning of the line...
    */

    for (value = line; isspace(*value & 255); value ++);

    for (nameptr = name; *value != '\0' && !isspace(*value & 255) &&
                             nameptr < (name + sizeof(name) - 1);)
      *nameptr++ = *value++;
    *nameptr = '\0';

    while (isspace(*value & 255))
      value ++;

    if (name[0] == '\0')
      continue;

   /*
    * Decode the directive...
    */

    if (strcasecmp(name, "</Location>") == 0)
      return (linenum);
    else if (strcasecmp(name, "<Limit") == 0 ||
             strcasecmp(name, "<LimitExcept") == 0)
    {
      if ((loc = CopyLocation(&parent)) == NULL)
        return (0);

      loc->limit = 0;
      while (*value)
      {
        for (valptr = value;
	     !isspace(*valptr & 255) && *valptr != '>' && *valptr;
	     valptr ++);

	if (*valptr)
	  *valptr++ = '\0';

        if (strcmp(value, "ALL") == 0)
	  loc->limit = AUTH_LIMIT_ALL;
	else if (strcmp(value, "GET") == 0)
	  loc->limit |= AUTH_LIMIT_GET;
	else if (strcmp(value, "HEAD") == 0)
	  loc->limit |= AUTH_LIMIT_HEAD;
	else if (strcmp(value, "OPTIONS") == 0)
	  loc->limit |= AUTH_LIMIT_OPTIONS;
	else if (strcmp(value, "POST") == 0)
	  loc->limit |= AUTH_LIMIT_POST;
	else if (strcmp(value, "PUT") == 0)
	  loc->limit |= AUTH_LIMIT_PUT;
	else if (strcmp(value, "TRACE") == 0)
	  loc->limit |= AUTH_LIMIT_TRACE;
	else
	  LogMessage(L_WARN, "Unknown request type %s on line %d!", value,
	             linenum);

        for (value = valptr; isspace(*value & 255) || *value == '>'; value ++);
      }

      if (strcasecmp(name, "<LimitExcept") == 0)
        loc->limit = AUTH_LIMIT_ALL ^ loc->limit;

      parent->limit &= ~loc->limit;
    }
    else if (strcasecmp(name, "</Limit>") == 0)
      loc = parent;
    else if (strcasecmp(name, "Encryption") == 0)
    {
     /*
      * "Encryption xxx" - set required encryption level...
      */

      if (strcasecmp(value, "never") == 0)
        loc->encryption = HTTP_ENCRYPT_NEVER;
      else if (strcasecmp(value, "always") == 0)
      {
        LogMessage(L_ERROR, "Encryption value \"%s\" on line %d is invalid in this context. "
	                    "Using \"required\" instead.", value, linenum);

        loc->encryption = HTTP_ENCRYPT_REQUIRED;
      }
      else if (strcasecmp(value, "required") == 0)
        loc->encryption = HTTP_ENCRYPT_REQUIRED;
      else if (strcasecmp(value, "ifrequested") == 0)
        loc->encryption = HTTP_ENCRYPT_IF_REQUESTED;
      else
        LogMessage(L_ERROR, "Unknown Encryption value %s on line %d.",
	           value, linenum);
    }
    else if (strcasecmp(name, "Order") == 0)
    {
     /*
      * "Order Deny,Allow" or "Order Allow,Deny"...
      */

      if (strncasecmp(value, "deny", 4) == 0)
        loc->order_type = AUTH_ALLOW;
      else if (strncasecmp(value, "allow", 5) == 0)
        loc->order_type = AUTH_DENY;
      else
        LogMessage(L_ERROR, "Unknown Order value %s on line %d.",
	           value, linenum);
    }
    else if (strcasecmp(name, "Allow") == 0 ||
             strcasecmp(name, "Deny") == 0)
    {
     /*
      * Allow [From] host/ip...
      * Deny [From] host/ip...
      */

      if (strncasecmp(value, "from", 4) == 0)
      {
       /*
        * Strip leading "from"...
	*/

	value += 4;

	while (isspace(*value & 255))
	  value ++;
      }

     /*
      * Figure out what form the allow/deny address takes:
      *
      *    All
      *    None
      *    *.domain.com
      *    .domain.com
      *    host.domain.com
      *    nnn.*
      *    nnn.nnn.*
      *    nnn.nnn.nnn.*
      *    nnn.nnn.nnn.nnn
      *    nnn.nnn.nnn.nnn/mm
      *    nnn.nnn.nnn.nnn/mmm.mmm.mmm.mmm
      */

      if (strcasecmp(value, "all") == 0)
      {
       /*
        * All hosts...
	*/

        if (strcasecmp(name, "Allow") == 0)
	  AllowIP(loc, 0, 0);
	else
	  DenyIP(loc, 0, 0);
      }
      else  if (strcasecmp(value, "none") == 0)
      {
       /*
        * No hosts...
	*/

        if (strcasecmp(name, "Allow") == 0)
	  AllowIP(loc, ~0, 0);
	else
	  DenyIP(loc, ~0, 0);
      }
      else if (value[0] == '*' || value[0] == '.' || !isdigit(value[0] & 255))
      {
       /*
        * Host or domain name...
	*/

	if (value[0] == '*')
	  value ++;

        if (strcasecmp(name, "Allow") == 0)
	  AllowHost(loc, value);
	else
	  DenyHost(loc, value);
      }
      else
      {
       /*
        * One of many IP address forms...
	*/

        memset(ip, 0, sizeof(ip));
        ipcount = sscanf(value, "%d.%d.%d.%d", ip + 0, ip + 1, ip + 2, ip + 3);
	address = (((((ip[0] << 8) | ip[1]) << 8) | ip[2]) << 8) | ip[3];

        if ((value = strchr(value, '/')) != NULL)
	{
	  value ++;
	  memset(mask, 0, sizeof(mask));
          switch (sscanf(value, "%d.%d.%d.%d", mask + 0, mask + 1,
	                 mask + 2, mask + 3))
	  {
	    case 1 :
	        netmask = (0xffffffff << (32 - mask[0])) & 0xffffffff;
	        break;
	    case 4 :
	        netmask = (((((mask[0] << 8) | mask[1]) << 8) |
		            mask[2]) << 8) | mask[3];
                break;
	    default :
        	LogMessage(L_ERROR, "Bad netmask value %s on line %d.",
	        	   value, linenum);
		netmask = 0xffffffff;
		break;
	  }
	}
	else
	  netmask = netmasks[ipcount - 1];

        if ((address & ~netmask) != 0)
	{
	  LogMessage(L_WARN, "Discarding extra bits in %s address %08x for netmask %08x...",
	             name, address, netmask);
          address &= netmask;
	}

        if (strcasecmp(name, "Allow") == 0)
	  AllowIP(loc, address, netmask);
	else
	  DenyIP(loc, address, netmask);
      }
    }
    else if (strcasecmp(name, "AuthType") == 0)
    {
     /*
      * AuthType {none,basic,digest,basicdigest}
      */

      if (strcasecmp(value, "none") == 0)
      {
	loc->type  = AUTH_NONE;
	loc->level = AUTH_ANON;
      }
      else if (strcasecmp(value, "basic") == 0)
      {
	loc->type = AUTH_BASIC;

        if (loc->level == AUTH_ANON)
	  loc->level = AUTH_USER;
      }
      else if (strcasecmp(value, "digest") == 0)
      {
	loc->type = AUTH_DIGEST;

        if (loc->level == AUTH_ANON)
	  loc->level = AUTH_USER;
      }
      else if (strcasecmp(value, "basicdigest") == 0)
      {
	loc->type = AUTH_BASICDIGEST;

        if (loc->level == AUTH_ANON)
	  loc->level = AUTH_USER;
      }
      else
        LogMessage(L_WARN, "Unknown authorization type %s on line %d.",
	           value, linenum);
    }
    else if (strcasecmp(name, "AuthClass") == 0)
    {
     /*
      * AuthClass anonymous, user, system, group
      */

      if (strcasecmp(value, "anonymous") == 0)
      {
        loc->type  = AUTH_NONE;
        loc->level = AUTH_ANON;
      }
      else if (strcasecmp(value, "user") == 0)
        loc->level = AUTH_USER;
      else if (strcasecmp(value, "group") == 0)
        loc->level = AUTH_GROUP;
      else if (strcasecmp(value, "system") == 0)
      {
        loc->level = AUTH_GROUP;

       /*
        * Use the default system group if none is defined so far...
	*/

        if (NumSystemGroups == 0)
	  NumSystemGroups = 1;

	for (i = 0; i < NumSystemGroups; i ++)
	  AddName(loc, SystemGroups[i]);
      }
      else
        LogMessage(L_WARN, "Unknown authorization class %s on line %d.",
	           value, linenum);
    }
    else if (strcasecmp(name, "AuthGroupName") == 0)
      AddName(loc, value);
    else if (strcasecmp(name, "Require") == 0)
    {
     /*
      * Apache synonym for AuthClass and AuthGroupName...
      *
      * Get initial word:
      *
      *     Require valid-user
      *     Require group names
      *     Require user names
      */

      for (valptr = value;
	   !isspace(*valptr & 255) && *valptr != '>' && *valptr;
	   valptr ++);

      if (*valptr)
	*valptr++ = '\0';

      if (strcasecmp(value, "valid-user") == 0 ||
          strcasecmp(value, "user") == 0)
        loc->level = AUTH_USER;
      else if (strcasecmp(value, "group") == 0)
        loc->level = AUTH_GROUP;
      else
      {
        LogMessage(L_WARN, "Unknown Require type %s on line %d.",
	           value, linenum);
	continue;
      }

     /*
      * Get the list of names from the line...
      */

      for (value = valptr; *value;)
      {
        for (valptr = value; !isspace(*valptr & 255) && *valptr; valptr ++);

	if (*valptr)
	  *valptr++ = '\0';

        AddName(loc, value);

        for (value = valptr; isspace(*value & 255); value ++);
      }
    }
    else if (strcasecmp(name, "Satisfy") == 0)
    {
      if (strcasecmp(value, "all") == 0)
        loc->satisfy = AUTH_SATISFY_ALL;
      else if (strcasecmp(value, "any") == 0)
        loc->satisfy = AUTH_SATISFY_ANY;
      else
        LogMessage(L_WARN, "Unknown Satisfy value %s on line %d.", value,
	           linenum);
    }
    else
      LogMessage(L_ERROR, "Unknown Location directive %s on line %d.",
	         name, linenum);
  }

  return (0);
}


/*
 * 'get_address()' - Get an address + port number from a line.
 */

static int					/* O - 1 if address good, 0 if bad */
get_address(char               *value,		/* I - Value string */
            unsigned           defaddress,	/* I - Default address */
	    int                defport,		/* I - Default port */
            struct sockaddr_in *address)	/* O - Socket address */
{
  char			hostname[256],		/* Hostname or IP */
			portname[256];		/* Port number or name */
  struct hostent	*host;			/* Host address */
  struct servent	*port;			/* Port number */  


 /*
  * Initialize the socket address to the defaults...
  */

  memset(address, 0, sizeof(struct sockaddr_in));

#ifdef HAVE_DOMAINSOCKETS
 /*
  * If the value begins with a / it's a Unix domain socket name
  */

  if (*value == '/')
  {
    if (strlen(value) >= sizeof(((struct sockaddr_un*)NULL)->sun_path))
    {
      LogMessage(L_ERROR, "Domain socket name too long \"%s\"!", value);
      return (0);
    }

    address->sin_family = AF_LOCAL;

   /*
    * Instead of copying the string into sockaddr_un.sun_path
    * we just store a pointer to the string in an sockaddr_in.
    */

    (char *)address->sin_addr.s_addr = strdup(value);
  }
  else
#endif /* HAVE_DOMAINSOCKETS */
  {
    address->sin_family      = AF_INET;
    address->sin_addr.s_addr = htonl(defaddress);
    address->sin_port        = htons(defport);

   /*
    * Try to grab a hostname and port number...
    */

    switch (sscanf(value, "%255[^:]:%255s", hostname, portname))
    {
      case 1 :
          if (strchr(hostname, '.') == NULL && defaddress == INADDR_ANY)
	  {
	   /*
	    * Hostname is a port number...
	    */

	    strlcpy(portname, hostname, sizeof(portname));
	    hostname[0] = '\0';
	  }
          else
            portname[0] = '\0';
          break;
      case 2 :
          break;
      default :
	  LogMessage(L_ERROR, "Unable to decode address \"%s\"!", value);
          return (0);
    }

   /*
    * Decode the hostname and port number as needed...
    */

    if (hostname[0] && strcmp(hostname, "*"))
    {
      if ((host = httpGetHostByName(hostname)) == NULL)
      {
        LogMessage(L_ERROR, "httpGetHostByName(\"%s\") failed - %s!", hostname,
                   hstrerror(h_errno));
        return (0);
      }

      memcpy(&(address->sin_addr), host->h_addr, host->h_length);
      address->sin_port = htons(defport);
    }

    if (portname[0] != '\0')
    {
      if (isdigit(portname[0]))
        address->sin_port = htons(atoi(portname));
      else
      {
        if ((port = getservbyname(portname, NULL)) == NULL)
        {
          LogMessage(L_ERROR, "getservbyname(\"%s\") failed - %s!", portname,
                     strerror(errno));
          return (0);
        }
        else
          address->sin_port = htons(port->s_port);
      }
    }
  }

  return (1);
}


/*
 * 'conf_file_check()' - Fix the mode and ownership of a file or directory. 
 */

static int
conf_file_check(const char*filename, 	/* I - File or directory name to test */
		const char *root, 	/* I - File or directory name prefix */
		int mode, 		/* I - File mode */
		int user, 		/* I - uid of owner */
		int group, 		/* I - gid og group */
		int is_dir,		/* I - Are we testing a file or directory? */
		int create_dir)		/* I - Should we create the directory if it's missing? */
{
  int		dir_created = 0;	/* Did we create a directory? */
  char		temp[1024];		/* File name with prefix */
  struct stat	sb;			/* Stat buffer */


 /*
  * Prepend the given root to the filename before testing it...
  */

  if (root)
  {
    snprintf(temp, sizeof(temp), "%s/%s", root, filename);
    filename = temp;
  }

  if (stat(filename, &sb) != 0)
  {
    if (errno == ENOENT && create_dir)
    {
      LogMessage(L_ERROR, "Creating missing directory \"%s\"", filename);
      if (mkdir(filename, mode) != 0)
      {
        LogMessage(L_ERROR, "Unable to create directory \"%s\" - %s!", filename,
			strerror(errno));
        return -1;
      }
      dir_created = 1;
    }
    else
      return -1;
  }

 /*
  * Make sure it's a regular file...
  */

  if (!dir_created && !is_dir && !S_ISREG(sb.st_mode))
  {
    LogMessage(L_ERROR, "\"%s\" is not a regular file!", filename);
    return -1;
  }

  if (!dir_created && is_dir && !S_ISDIR(sb.st_mode))
  {
    LogMessage(L_ERROR, "\"%s\" is not a directory!", filename);
    return -1;
  }

 /*
  * Fix owner & mode flags...
  */

  if (dir_created || sb.st_uid != user || sb.st_gid != group)
  {
    LogMessage(L_WARN, "Repairing ownership of \"%s\"", filename);
    chown(filename, user, group);
  }

  if (dir_created || (sb.st_mode & ALLPERMS) != mode)
  {
    LogMessage(L_WARN, "Repairing access permissions of \"%s\"", filename);
    chmod(filename, mode);
  }

  return 0;
}


/*
 * End of "$Id: conf.c,v 1.45.2.1 2006/12/05 22:11:09 jlovell Exp $".
 */
