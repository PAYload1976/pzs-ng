#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "zsfunctions.h"
#include "helpfunctions.h"
#include "race-file.h"
#include "objects.h"
#include "macros.h"
#include "multimedia.h"
#include "convert.h"
#include "dizreader.h"
#include "stats.h"
#include "complete.h"
#include "crc.h"
#include "ng-version.h"

#include "../conf/zsconfig.h"
#include "../include/zsconfig.defaults.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifndef HAVE_STRLCPY
# include "strl/strl.h"
#endif


int 
main()
{
	int		n, m, l, complete_type = 0, gnum = 0, unum = 0, f_id = 0;
	char           *ext, exec[4096], *complete_bar = 0;
	unsigned int	crc;
	struct stat	fileinfo;

	uid_t		f_uid;
	gid_t		f_gid;
	double		temp_time = 0;

	DIR		*dir, *parent;
	struct dirent	*dp;

	GLOBAL		g;

#if ( program_uid > 0 )
	setegid(program_gid);
	seteuid(program_uid);
#endif

	umask(0666 & 000);

	d_log("PZS-NG (rescan) v%s debug log.\n", ng_version());
	d_log("Allocating memory for variables\n");
	g.ui = malloc(sizeof(struct USERINFO *) * 30);
	memset(g.ui, 0, sizeof(struct USERINFO *) * 30);
	g.gi = malloc(sizeof(struct GROUPINFO *) * 30);
	memset(g.gi, 0, sizeof(struct GROUPINFO *) * 30);

	getcwd(g.l.path, PATH_MAX);

	if ((matchpath(nocheck_dirs, g.l.path) || (!matchpath(zip_dirs, g.l.path) && !matchpath(sfv_dirs, g.l.path) && !matchpath(group_dirs, g.l.path))) && rescan_nocheck_dirs_allowed == FALSE) {
		d_log("Dir matched with nocheck_dirs, or is not in the zip/sfv/group-dirs\n");
		d_log("Freeing memory, and exiting\n");
		free(g.ui);
		free(g.gi);
		return 0;
	}
	g.v.misc.slowest_user[0] = 30000;

	bzero(&g.v.total, sizeof(struct race_total));
	g.v.misc.fastest_user[0] = 0;
	g.v.misc.release_type = RTYPE_NULL;

	if (getenv("SECTION") == NULL) {
		sprintf(g.v.sectionname, "DEFAULT");
	} else {
		snprintf(g.v.sectionname, 127, getenv("SECTION"));
	}

	g.l.race = malloc(n = strlen(g.l.path) + 10 + sizeof(storage));
	g.l.sfv = malloc(n);
	g.l.leader = malloc(n);
	g.l.length_path = strlen(g.l.path);
	g.l.length_zipdatadir = sizeof(storage);

	getrelname(&g);
	gnum = buffer_groups(GROUPFILE, 0);
	unum = buffer_users(PASSWDFILE, 0);

	sprintf(g.l.sfv, storage "/%s/sfvdata", g.l.path);
	sprintf(g.l.leader, storage "/%s/leader", g.l.path);
	sprintf(g.l.race, storage "/%s/racedata", g.l.path);

	//rescandir(2);
	move_progress_bar(1, &g.v, g.ui, g.gi);
	if (g.l.incomplete)
		unlink(g.l.incomplete);
	removecomplete();
	if (g.l.race)
		unlink(g.l.race);
	if (g.l.sfv)
		unlink(g.l.sfv);
	printf("Rescanning files...\n");
	
	dir = opendir(".");
	parent = opendir("..");

	if (findfileext(dir, ".sfv")) {
		strlcpy(g.v.file.name, findfileext(dir, ".sfv"), PATH_MAX);
		maketempdir(g.l.path);
		stat(g.v.file.name, &fileinfo);
		if (copysfv(g.v.file.name, g.l.sfv)) {
			printf("Found invalid entries in SFV - Exiting.\n");

			while ((dp = readdir(dir))) {
				m = l = strlen(dp->d_name);
				ext = find_last_of(dp->d_name, "-");
				if (!strncmp(ext, "-missing", 8))
					unlink(dp->d_name);
			}

			d_log("Freeing memory, and exiting\n");
			unlink(g.l.sfv);
			unlink(g.l.race);
			free(g.ui);
			free(g.gi);
			free(g.l.race);
			free(g.l.sfv);
			free(g.l.leader);
			
			return 0;
		}
		g.v.total.start_time = 0;
		while ((dp = readdir(dir))) {
			m = l = strlen(dp->d_name);

			ext = find_last_of(dp->d_name, ".");
			if (*ext == '.')
				ext++;

			if (!strcomp(ignored_types, ext) && (!(strcomp(allowed_types, ext) &&
				!matchpath(allowed_types_exemption_dirs, g.l.path))) && strcasecmp("sfv", ext) &&
				strcasecmp("nfo", ext) && strcasecmp("bad", ext) && strcmp(dp->d_name + l - 8, "-missing") &&
				strncmp(dp->d_name, ".", 1)) {
				
				stat(dp->d_name, &fileinfo);
				f_uid = fileinfo.st_uid;
				f_gid = fileinfo.st_gid;

				strcpy(g.v.user.name, get_u_name(f_uid));
				strcpy(g.v.user.group, get_g_name(f_gid));
				strlcpy(g.v.file.name, dp->d_name, PATH_MAX);
				g.v.file.speed = 2005 * 1024;
				g.v.file.size = fileinfo.st_size;

				temp_time = fileinfo.st_mtime;
				if (g.v.total.start_time == 0)
					g.v.total.start_time = temp_time;
				else
					g.v.total.start_time = (g.v.total.start_time < temp_time ? g.v.total.start_time : temp_time);
				g.v.total.stop_time = (temp_time > g.v.total.stop_time ? temp_time : g.v.total.stop_time);

				/* Hide users in group_dirs */
				if (matchpath(group_dirs, g.l.path) && (hide_group_uploaders == TRUE)) {
					d_log("Hiding user in group-dir:\n");
					if (strlen(hide_gname) > 0) {
						snprintf(g.v.user.group, 18, "%s", hide_gname);
						d_log("   Changing groupname\n");
					}
					if (strlen(hide_uname) > 0) {
						snprintf(g.v.user.name, 18, "%s", hide_uname);
						d_log("   Changing username\n");
					}
					if (strlen(hide_uname) == 0) {
						d_log("   Making username = groupname\n");
						snprintf(g.v.user.name, 18, "%s", g.v.user.group);
					}
				}

				crc = calc_crc32(dp->d_name);
				if (!S_ISDIR(fileinfo.st_mode)) {
					if (g.v.file.name)
						unlink_missing(g.v.file.name);
					if (l > 44) {
						printf("\nFile: %s %.8x", dp->d_name + l - 44, crc);
					} else {
						printf("\nFile: %-44s %.8x", dp->d_name, crc);
					}
				}
				if(fflush(stdout))
					d_log("ERROR: %s\n", strerror(errno));
				writerace(g.l.race, &g.v, crc, F_NOTCHECKED);
			}
		}
		printf("\n");
		testfiles(&g.l, &g.v, 1);
		printf("\n");

		//rescandir(2);	/* We need to rescan again */
		readsfv(g.l.sfv, &g.v, 0);
		readrace(g.l.race, &g.v, g.ui, g.gi);
		sortstats(&g.v, g.ui, g.gi);
		buffer_progress_bar(&g.v);

		if (g.l.nfo_incomplete) {
			if (findfileext(dir, ".nfo")) {
				d_log("Removing missing-nfo indicator (if any)\n");
				remove_nfo_indicator(&g);
			} else if (matchpath(check_for_missing_nfo_dirs, g.l.path) && (!matchpath(group_dirs, g.l.path) || create_incomplete_links_in_group_dirs)) {
				if (!g.l.in_cd_dir) {
					d_log("Creating missing-nfo indicator %s.\n", g.l.nfo_incomplete);
					create_incomplete_nfo();
				} else {
					if (findfileextparent(parent, ".nfo")) {
						d_log("Removing missing-nfo indicator (if any)\n");
						remove_nfo_indicator(&g);
					} else {
						d_log("Creating missing-nfo indicator (base) %s.\n", g.l.nfo_incomplete);
						create_incomplete_nfo();
					}
				}
			}
		}
		if (g.v.misc.release_type == RTYPE_AUDIO)
			get_mpeg_audio_info(findfileext(dir, ".mp3"), &g.v.audio);

		if ((g.v.total.files_missing == 0) & (g.v.total.files > 0)) {
			switch (g.v.misc.release_type) {
			case RTYPE_RAR:
				complete_bar = rar_completebar;
				break;
			case RTYPE_OTHER:
				complete_bar = other_completebar;
				break;
			case RTYPE_AUDIO:
				complete_bar = audio_completebar;
#if ( enabled_create_m3u )
				n = sprintf(exec, findfileext(dir, ".sfv"));
				strcpy(exec + n - 3, "m3u");
				create_indexfile(&g.l, &g.v, exec);
#endif
				break;
			case RTYPE_VIDEO:
				complete_bar = video_completebar;
				break;
			}
			g.v.misc.write_log = 0;
			complete(&g, complete_type);
			createstatusbar(convert(&g.v, g.ui, g.gi, complete_bar));
#if (chmod_completebar)
			if (!matchpath(group_dirs, g.l.path)) {
				chmod(convert(&g.v, g.ui, g.gi, complete_bar), 0222);
			}
#endif
		} else {
			if (!matchpath(group_dirs, g.l.path) || create_incomplete_links_in_group_dirs) {
				create_incomplete();
			}
				move_progress_bar(0, &g.v, g.ui, g.gi);
		}
	} else if (findfileext(dir, ".zip")) {
		strlcpy(g.v.file.name, findfileext(dir, ".zip"), PATH_MAX);
		maketempdir(g.l.path);
		stat(g.v.file.name, &fileinfo);
		//n = direntries;
		crc = 0;
		rewinddir(dir);
		while ((dp = readdir(dir))) {
			m = l = strlen(dp->d_name);
			
			ext = find_last_of(dp->d_name, ".");
			if (*ext == '.')
				ext++;

			if (!strcasecmp(ext, "zip")) {
				stat(dp->d_name, &fileinfo);
				f_uid = fileinfo.st_uid;
				f_gid = fileinfo.st_gid;

				strcpy(g.v.user.name, get_u_name(f_uid));
				strcpy(g.v.user.group, get_g_name(f_gid));
				strlcpy(g.v.file.name, dp->d_name, PATH_MAX);
				g.v.file.speed = 2005 * 1024;
				g.v.file.size = fileinfo.st_size;
				g.v.total.start_time = 0;

				if (!fileexists("file_id.diz")) {
					sprintf(exec, "%s -qqjnCLL %s file_id.diz", unzip_bin, g.v.file.name);
					if (execute(exec) != 0) {
						d_log("No file_id.diz found (#%d): %s\n", errno, strerror(errno));
					} else {
						if ((f_id = findfile("file_id.diz.bad")))
							unlink(dp->d_name);
						chmod("file_id.diz", 0666);
					}
				}
				sprintf(exec, "%s -qqt %s &> /dev/null", unzip_bin, g.v.file.name);
				if (system(exec) == 0) {
					writerace(g.l.race, &g.v, crc, F_CHECKED);
				} else {
					writerace(g.l.race, &g.v, crc, F_BAD);
					if (g.v.file.name)
						unlink(g.v.file.name);
				}
			}
		}
		g.v.total.files = read_diz("file_id.diz");
		if (!g.v.total.files) {
			g.v.total.files = 1;
			unlink("file_id.diz");
		}
		g.v.total.files_missing = g.v.total.files;
		readrace(g.l.race, &g.v, g.ui, g.gi);
		sortstats(&g.v, g.ui, g.gi);
		if (g.v.total.files_missing < 0) {
			g.v.total.files -= g.v.total.files_missing;
			g.v.total.files_missing = 0;
		}
		buffer_progress_bar(&g.v);
		if (g.v.total.files_missing == 0) {
			g.v.misc.write_log = 0;
			complete(&g, complete_type);
			createstatusbar(convert(&g.v, g.ui, g.gi, zip_completebar));
#if (chmod_completebar)
			if (!matchpath(group_dirs, g.l.path)) {
				chmod(convert(&g.v, g.ui, g.gi, zip_completebar), 0222);
			}
#endif

		} else {
			if (!matchpath(group_dirs, g.l.path) || create_incomplete_links_in_group_dirs) {
				create_incomplete();
			}
				move_progress_bar(0, &g.v, g.ui, g.gi);
		}
		if (g.l.nfo_incomplete) {
			if (findfileext(dir, ".nfo")) {
				d_log("Removing missing-nfo indicator (if any)\n");
				remove_nfo_indicator(&g);
			} else if (matchpath(check_for_missing_nfo_dirs, g.l.path) && (!matchpath(group_dirs, g.l.path) || create_incomplete_links_in_group_dirs)) {
				if (!g.l.in_cd_dir) {
					d_log("Creating missing-nfo indicator %s.\n", g.l.nfo_incomplete);
					create_incomplete_nfo();
				} else {
					if (findfileextparent(parent, ".nfo")) {
						d_log("Removing missing-nfo indicator (if any)\n");
						remove_nfo_indicator(&g);
					} else {
						d_log("Creating missing-nfo indicator (base) %s.\n", g.l.nfo_incomplete);
						create_incomplete_nfo();
					}
				}
			}
		}
	} else if (mark_empty_dirs_as_incomplete_on_rescan) {
			create_incomplete();
			printf(" Empty dir - marking as incomplete.\n");
	}
	printf(" Passed : %i\n", (int)g.v.total.files - (int)g.v.total.files_missing);
	printf(" Failed : %i\n", (int)g.v.total.files_bad);
	printf(" Missing: %i\n", (int)g.v.total.files_missing);
	printf("  Total : %i\n", (int)g.v.total.files);

	d_log("Freeing memory.\n");
	closedir(dir);
	closedir(parent);
	//rescandir(1);
	updatestats_free(g.v, g.ui, g.gi);
	free(g.l.race);
	free(g.l.sfv);
	free(g.l.leader);
	//free(g.v.misc.release_name);
	//free(g.l.link_source);
	buffer_groups(GROUPFILE, gnum);
	buffer_users(PASSWDFILE, unum);

	exit(0);
}
