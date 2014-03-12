#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <linux/kd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/if.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <linux/loop.h>
#include <cutils/partition_utils.h>
#include <cutils/properties.h>
#include <cutils/android_reboot.h>
#include "log.h"
#include "md5.h"

#ifndef MD5_DIGEST_LENGTH
#define MD5_DIGEST_LENGTH 16
#endif

extern int e2fsck_main(int argc, char *argv[]);

//data only check time in bootup:
#define INTERVAL_IN_BOOT 5

//data only check time in normal:
#define INTERVAL_AFTER_BOOT 180

//system partition check time: CHECK_SYSTEM_COUNT * "data only check time"
#define CHECK_SYSTEM_COUNT 5

//sleep between system file check, unit is us
#define CHECK_SYSTEM_FILE_SLEEP_TIME 5000

#define CHECKSUM_LIST_PATH "/system/chksum_list"

#define SYSTEM_BAK_NODE "/dev/block/backup"

int g_bootCompleted = 0;
int g_supportSysBak = 0;

int main(int argc, char **argv)
{
	int nCheckInterval = INTERVAL_IN_BOOT;
	int nCheckSystemCount = CHECK_SYSTEM_COUNT; 
	klog_init();

	ERROR("data_integrity_guard main!\n");

	g_supportSysBak = is_support_system_bak();	
	
	while( 1 ) {
		if( g_bootCompleted == 0 ) {
                        if( isBootCompleted() == 1 )
                        {
                                g_bootCompleted = 1;
                                nCheckInterval = INTERVAL_AFTER_BOOT;
                        }
                }

		//check data ro
		int ro = is_data_ro();
		if( ro == 1 ) 
		{
#if 0
			//e2fsck
			do_e2fsck( 2, "-y", "/dev/block/data");			
	
			//remount
			do_remount("/dev/block/data", "/data", "ext4", 0);
	
			//read again	
			ro = is_data_ro();

			if( ro == 1 ) 
			{
				handleDataRo();
		        }
#else
			handleDataRo();
#endif
		}

		//check system partition
		if( ( g_supportSysBak != 0 ) && ( nCheckSystemCount++ == CHECK_SYSTEM_COUNT ) ) {
			nCheckSystemCount = 1;
			//ERROR("check_system_partition before\n");
			char error_file_path[512];
			int sys_check = check_system_partition(error_file_path);
			//ERROR("check_system_partition after\n");
			if( sys_check != 0 ) {
				HanldeSysChksumError(error_file_path);
			}
		}

		
		//sleep
		sleep(nCheckInterval);	
	}	

	ERROR("data_integrity_guard exit!\n");
	
	return 0;
}

void HanldeSysChksumError(char* error_file_path) {
	if( g_bootCompleted == 0 ) {
		//reboot into recovery to restore system
		ERROR("HanldeSysChksumError do_restore_system\n");
		do_restore_system();
	} else {
		//start notify activity,RestoreSystemActivity 
		ERROR("HanldeSysChksumError start notify activity,RestoreSystemActivity\n");
		char* cmd = NULL;
		asprintf(&cmd, "/system/bin/am start -n com.amlogic.promptuser/com.amlogic.promptuser.RestoreSystemActivity -e error_file_path %s", error_file_path);
		if( cmd != NULL ) {
			system(cmd);
			free(cmd);
		} else {
			system("/system/bin/am start -n com.amlogic.promptuser/com.amlogic.promptuser.RestoreSystemActivity");
		}
	}
}

void handleDataRo() {
	if( g_bootCompleted == 0 ) {
		//reboot
		ERROR("handleDataRo do_reboot\n");
		do_reboot();
        } else {
                //start notify activity,RebootActivity  
		ERROR("handleDataRo start notify activity,RebootActivity\n");
                system("/system/bin/am start -n com.amlogic.promptuser/com.amlogic.promptuser.RebootActivity");
        }
}

void do_remount( char* dev, char* target, char* system, int readonly ) {
	ERROR("data_integrity_guard do_remount!\n");
	int flags = MS_REMOUNT | MS_NOATIME | MS_NODIRATIME | MS_NOSUID | MS_NODEV;
	if(readonly == 1) {
		flags |= MS_RDONLY;
	}
	char options[] = "noauto_da_alloc";
	mount( dev, target, system, flags, options);	
}

void do_reboot() 
{
	ERROR("data_integrity_guard do_reboot!\n");
	android_reboot(ANDROID_RB_RESTART, 0, 0);
	sleep(20);
}

void do_restore_system()
{
	ERROR("data_integrity_guard do_restore_system!\n");

	mkdir("/cache/recovery", 0666);

	//write command
	FILE *fCommand = NULL;
	if( (fCommand = fopen("/cache/recovery/command", "wt")) == NULL ) {
		ERROR("data_integrity_guard create /cache/recovery/command fail!\n");
		return;
	}

	fprintf(fCommand, "--restore_system\n");

	fclose(fCommand);	

	android_reboot(ANDROID_RB_RESTART2, 0, "recovery");
		
	sleep(20);	
}

int u_read(int  fd, void*  buff, int  len)
{
    int  ret;
    do { ret = read(fd, buff, len); } while (ret < 0 && errno == EINTR);
    return ret;
}

int f_read(const char*  filename, char* buff, size_t  buffsize)
{
    int  len = 0;
    int  fd  = open(filename, O_RDONLY);
    if (fd >= 0) {
        len = u_read(fd, buff, buffsize-1);
        close(fd);
    }
    buff[len > 0 ? len : 0] = 0;
    return len;
}


int is_data_ro()
{
    int ro = 0;
    char mounts[2048], *start, *end, *line;
    f_read("/proc/mounts", mounts, sizeof(mounts));
    start = mounts;

    while( (end = strchr(start, '\n')))
    {
        line = start;
        *end++ = 0;
        start = end;

        //ERROR("line:%s\n",line);

        if( strstr( line, "/data" ) != NULL )
        {
            //ERROR("data line:%s\n", line);
            if( strstr( line, "ro" ) != NULL )
            {
                ERROR("data partition is read-only!\n");
                ro = 1;
            }
            break;
        }
    }

    //ERROR("is_data_ro ret:%d\n",ro);

    return ro;
}

int do_e2fsck(int nargs, char **args) {

    ERROR("data_integrity_guard do_e2fsck!\n");
    
    if (nargs == 3) {
        ERROR("Before e2fsck_main...\n");

        e2fsck_main(nargs, args);

        ERROR("After e2fsck_main...\n");

    } else {
        ERROR("e2fsck bad args %d.", nargs);
    }

    return 0;
}

int isBootCompleted() {
	int ret = 0;

	//check if system is complete
        char flag[PROPERTY_VALUE_MAX];
        property_get("sys.boot_completed", flag, "");
	if(strcmp(flag, "1") == 0){
        	ERROR("data_integrity_guard isBootCompleted:%s!\n", flag);
		ret = 1;
        }

	return ret;
}

int check_system_partition(char* error_file_path) {
	FILE* f_chk_sum = NULL;
	if( (f_chk_sum = fopen(CHECKSUM_LIST_PATH, "r")) == NULL ) {
		ERROR("check_system_partition fopen chksum_list fail!\n");
		return 0;
	}

	char chksum[256], filepath[256];
	while( fscanf(f_chk_sum,"%s  %s\n", chksum, filepath) == 2 )
 	{
 		//ERROR("check_system_partition chksum:%s -> filepath:%s\n", chksum, filepath);
		unsigned char md5[MD5_DIGEST_LENGTH];
		char md5_str[2*MD5_DIGEST_LENGTH+1] = {0};
		if( get_md5(filepath, md5) == 0 ) {
                        hextoa( md5_str, md5, MD5_DIGEST_LENGTH );	
		} else {
			ERROR("check_system_partition get md5sum fail filepath:%s\n", filepath);
			sprintf( error_file_path, "%s", filepath);
                        return 1;
		}

		if( strcmp(chksum, md5_str) ) {
  			ERROR("check_system_partition chksum is wrong filepath:%s !\n", filepath);
			sprintf( error_file_path, "%s", filepath);
			return 1;
		}
		
		usleep(CHECK_SYSTEM_FILE_SLEEP_TIME);
 	}		

	fclose(f_chk_sum);

	return 0;
}

void hextoa(char *szBuf, unsigned char nData[], int len)
{
        int i;
        for( i = 0; i < len; i++,szBuf+=2 ) {
                sprintf(szBuf,"%02x",nData[i]);
        }
}

int get_md5(const char *path, unsigned char* md5)
{
    unsigned int i;
    int fd;
    MD5_CTX md5_ctx;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr,"could not open %s, %s\n", path, strerror(errno));
        return -1;
    }

    /* Note that bionic's MD5_* functions return void. */
    MD5_Init(&md5_ctx);

    while (1) {
        char buf[4096];
        ssize_t rlen;
        rlen = read(fd, buf, sizeof(buf));
        if (rlen == 0)
            break;
        else if (rlen < 0) {
            fprintf(stderr,"could not read %s, %s\n", path, strerror(errno));
            return -1;
        }
        MD5_Update(&md5_ctx, buf, rlen);
    }
    if (close(fd)) {
        fprintf(stderr,"could not close %s, %s\n", path, strerror(errno));
    }

    MD5_Final(md5, &md5_ctx);
/*
    for (i = 0; i < (int)sizeof(md5); i++)
        printf("%02x", md5[i]);
    printf("  %s\n", path);
*/
    return 0;
}

int is_support_system_bak() {
	int sup_sys_bak = 0;
	char system_bak_enable[PROPERTY_VALUE_MAX];
        property_get("ro.system_backup_enable", system_bak_enable, "0");
        if(strcmp(system_bak_enable, "1") == 0){
		sup_sys_bak = is_file_exist( SYSTEM_BAK_NODE );
	}
	ERROR("is_support_system_bak sup_sys_bak:%d\n", sup_sys_bak);
	return sup_sys_bak;
}

int is_file_exist(const char* path) {
	return ( access( path, F_OK ) == 0 );
}
