// Sasha Jenner

#include <getopt.h>
#include <sys/wait.h>

#include <string>
#include <vector>

#include "slow5.h"
#include "error.h"
#include "cmd.h"
#include "slow5_extra.h"
#include "read_fast5.h"


#define USAGE_MSG "Usage: %s [OPTION]... [FAST5_FILE/DIR]...\n"
#define HELP_SMALL_MSG "Try '%s --help' for more information.\n"
#define HELP_LARGE_MSG \
    "Convert fast5 file(s) to slow5 or (compressed) blow5.\n" \
    USAGE_MSG \
    "\n" \
    "OPTIONS:\n" \
    "    -b, --binary               convert to blow5\n" \
    "    -c, --compress             convert to compressed blow5\n" \
    "    -h, --help                 display this message and exit\n" \
    "    --iop INT                  number of I/O processes to read fast5 files\n" \
    "    -l, --lossy                do not store auxiliary fields\n" \
    "    -d, --output_dir=[dir]     output directory where slow5files are written to\n" \

static double init_realtime = 0;

// what a child process should do, i.e. open a tmp file, go through the fast5 files
void f2s_child_worker(enum slow5_fmt format_out, enum press_method pressMethod, int lossy, proc_arg_t args, std::vector<std::string>& fast5_files, char* output_dir, struct program_meta *meta, reads_count* readsCount){

    static size_t call_count = 0;
    slow5_file_t* slow5File;
    FILE *slow5_file_pointer = NULL;
    std::string slow5_path;
    if(output_dir){
        slow5_path = std::string(output_dir);
    }
    fast5_file_t fast5_file;

//    fprintf(stderr,"starti %d\n",args.starti);
    for (int i = args.starti; i < args.endi; i++) {
        readsCount->total_5++;
        fast5_file = fast5_open(fast5_files[i].c_str());
        fast5_file.fast5_path = fast5_files[i].c_str();

        if (fast5_file.hdf5_file < 0){
            WARNING("Fast5 file [%s] is unreadable and will be skipped", fast5_files[i].c_str());
            H5Fclose(fast5_file.hdf5_file);
            readsCount->bad_5_file++;
            continue;
        }

        if(output_dir){
            if (fast5_file.is_multi_fast5) {
                std::string slow5file = fast5_files[i].substr(fast5_files[i].find_last_of('/'),
                                                              fast5_files[i].length() -
                                                              fast5_files[i].find_last_of('/') - 6) + ".slow5";
                slow5_path += slow5file;
                //fprintf(stderr,"slow5path = %s\n fast5_path = %s\nslow5file = %s\n",slow5_path.c_str(), fast5_files[i].c_str(),slow5file.c_str());

                slow5_file_pointer = fopen(slow5_path.c_str(), "w");

                // An error occured
                if (!slow5_file_pointer) {
                    ERROR("File '%s' could not be opened - %s.",
                          slow5_path.c_str(), strerror(errno));
                    continue;
                }
                slow5File = slow5_init_empty(slow5_file_pointer, slow5_path.c_str(), FORMAT_ASCII);
                slow5_hdr_initialize(slow5File->header, lossy);
                read_fast5(&fast5_file, format_out, pressMethod, lossy, call_count, meta, slow5File);

            }else{ // single-fast5
                if(!slow5_file_pointer){
                    slow5_path += "/"+std::to_string(args.starti)+".slow5";
                    if(call_count==0){
                        slow5_file_pointer = fopen(slow5_path.c_str(), "w");
                    }else{
                        slow5_file_pointer = fopen(slow5_path.c_str(), "a");
                    }
                    // An error occured
                    if (!slow5_file_pointer) {
                        ERROR("File '%s' could not be opened - %s.",
                              slow5_path.c_str(), strerror(errno));
                        continue;
                    }
                }
                slow5File = slow5_init_empty(slow5_file_pointer, slow5_path.c_str(), FORMAT_ASCII);
                slow5_hdr_initialize(slow5File->header, lossy);
                read_fast5(&fast5_file, format_out, pressMethod, lossy, call_count++, meta, slow5File);

            }
        } else{
            slow5File = slow5_init_empty(stdout, slow5_path.c_str(), FORMAT_ASCII);
            slow5_hdr_initialize(slow5File->header, lossy);
            if (fast5_file.is_multi_fast5) {
                read_fast5(&fast5_file, format_out, pressMethod, lossy, call_count, meta, slow5File);
            }else{
                read_fast5(&fast5_file, format_out, pressMethod, lossy, call_count++, meta, slow5File);
            }
        }

        H5Fclose(fast5_file.hdf5_file);
        if(output_dir && fast5_file.is_multi_fast5){
            slow5_path = std::string(output_dir);
            if(format_out == FORMAT_BINARY){
                slow5_eof_fwrite(slow5File->fp);
            }
            slow5_close(slow5File);
        }
    }
    if(output_dir && !fast5_file.is_multi_fast5) {
        if(format_out == FORMAT_BINARY){
            slow5_eof_fwrite(slow5File->fp);
        }
        slow5_close(slow5File);
    }
    if(meta->verbose){
        fprintf(stderr, "The processed - total fast5: %lu, bad fast5: %lu\n", readsCount->total_5, readsCount->bad_5_file);
    }
}

void f2s_iop(enum slow5_fmt format_out, enum press_method pressMethod, int lossy, int iop, std::vector<std::string>& fast5_files, char* output_dir, struct program_meta *meta, reads_count* readsCount){
    double realtime0 = slow5_realtime();
    int64_t num_fast5_files = fast5_files.size();

    //create processes
    std::vector<pid_t> pids_v(iop);
    std::vector<proc_arg_t> proc_args_v(iop);
    pid_t *pids = pids_v.data();
    proc_arg_t *proc_args = proc_args_v.data();

    int32_t t;
    int32_t i = 0;
    int32_t step = (num_fast5_files + iop - 1) / iop;
    //todo : check for higher num of procs than the data
    //current works but many procs are created despite

    //set the data structures
    for (t = 0; t < iop; t++) {
        proc_args[t].starti = i;
        i += step;
        if (i > num_fast5_files) {
            proc_args[t].endi = num_fast5_files;
        } else {
            proc_args[t].endi = i;
        }
        proc_args[t].proc_index = t;
    }

    if(iop==1){
        f2s_child_worker(format_out, pressMethod, lossy, proc_args[0],fast5_files, output_dir, meta, readsCount);
//        goto skip_forking;
        return;
    }

    //create processes
    STDERR("Spawning %d I/O processes to circumvent HDF hell", iop);
    for(t = 0; t < iop; t++){
        pids[t] = fork();

        if(pids[t]==-1){
            ERROR("%s","Fork failed");
            perror("");
            exit(EXIT_FAILURE);
        }
        if(pids[t]==0){ //child
            f2s_child_worker(format_out, pressMethod, lossy, proc_args[t],fast5_files,output_dir, meta, readsCount);
            exit(EXIT_SUCCESS);
        }
        if(pids[t]>0){ //parent
            continue;
        }
    }

    //wait for processes
    int status,w;
    for (t = 0; t < iop; t++) {
//        if(opt::verbose>1){
//            STDERR("parent : Waiting for child with pid %d",pids[t]);
//        }
        w = waitpid(pids[t], &status, 0);
        if (w == -1) {
            ERROR("%s","waitpid failed");
            perror("");
            exit(EXIT_FAILURE);
        }
        else if (WIFEXITED(status)){
//            if(opt::verbose>1){
//                STDERR("child process %d exited, status=%d", pids[t], WEXITSTATUS(status));
//            }
            if(WEXITSTATUS(status)!=0){
                ERROR("child process %d exited with status=%d",pids[t], WEXITSTATUS(status));
                exit(EXIT_FAILURE);
            }
        }
        else {
            if (WIFSIGNALED(status)) {
                ERROR("child process %d killed by signal %d", pids[t], WTERMSIG(status));
            } else if (WIFSTOPPED(status)) {
                ERROR("child process %d stopped by signal %d", pids[t], WSTOPSIG(status));
            } else {
                ERROR("child process %d did not exit propoerly: status %d", pids[t], status);
            }
            exit(EXIT_FAILURE);
        }
    }
//    skip_forking:

    fprintf(stderr, "[%s] Parallel converting to slow5 is done - took %.3fs\n", __func__,  slow5_realtime() - realtime0);

}

void recurse_dir(const char *f_path, enum slow5_fmt format_out, enum press_method pressMethod, int lossy, reads_count* readsCount, char* output_dir, struct program_meta *meta) {

    DIR *dir;
    struct dirent *ent;

    dir = opendir(f_path);

    if (dir == NULL) {
        if (errno == ENOTDIR) {
            // If it has the fast5 extension
            if (std::string(f_path).find(FAST5_EXTENSION)!= std::string::npos){
                std::vector<std::string> fast5_files;
                fast5_files.push_back(f_path);
                f2s_iop(format_out, pressMethod, lossy, 1, fast5_files, output_dir, meta, readsCount);
            }

        } else {
            WARNING("File '%s' failed to open - %s.",
                    f_path, strerror(errno));
        }

    } else {
        fprintf(stderr, "[%s::%.3f*%.2f] Extracting fast5 from %s\n", __func__,
                slow5_realtime() - init_realtime, slow5_cputime() / (slow5_realtime() - init_realtime), f_path);

        // Iterate through sub files
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") != 0 &&
                strcmp(ent->d_name, "..") != 0) {

                // Make sub path string
                // f_path + '/' + ent->d_name + '\0'
                size_t sub_f_path_len = strlen(f_path) + 1 + strlen(ent->d_name) + 1;
                char *sub_f_path = (char *) malloc(sizeof *sub_f_path * sub_f_path_len);
                MALLOC_CHK(sub_f_path);
                snprintf(sub_f_path, sub_f_path_len, "%s/%s", f_path, ent->d_name);

                // Recurse
                recurse_dir(sub_f_path, format_out, pressMethod, lossy, readsCount, output_dir, meta);

                free(sub_f_path);
                sub_f_path = NULL;
            }
        }

        closedir(dir);
    }
}

int f2s_main(int argc, char **argv, struct program_meta *meta) {
    int ret; // For checking return values of functions
    int iop = 1;
    int lossy = 0;

    // Debug: print arguments
    if (meta != NULL && meta->debug) {
        if (meta->verbose) {
            VERBOSE("printing the arguments given%s","");
        }
        fprintf(stderr, DEBUG_PREFIX "argv=[",
                __FILE__, __func__, __LINE__);
        for (int i = 0; i < argc; ++ i) {
            fprintf(stderr, "\"%s\"", argv[i]);
            if (i == argc - 1) {
                fprintf(stderr, "]");
            } else {
                fprintf(stderr, ", ");
            }
        }
        fprintf(stderr, NO_COLOUR);
    }

    // No arguments given
    if (argc <= 1) {
        fprintf(stderr, HELP_LARGE_MSG, argv[0]);
        EXIT_MSG(EXIT_FAILURE, argv, meta);
        return EXIT_FAILURE;
    }

    static struct option long_opts[] = {
        {"binary", no_argument, NULL, 'b'},    //0
        {"compress", no_argument, NULL, 'c'},  //1
        {"help", no_argument, NULL, 'h'},  //2
        {"output", required_argument, NULL, 'o'},   //3
        { "iop", required_argument, NULL, 0}, //4
        { "lossy", no_argument, NULL, 'l'}, //4
        { "output_dir", required_argument, NULL, 'd'}, //5
        {NULL, 0, NULL, 0 }
    };

    // Default options
    enum slow5_fmt format_out = FORMAT_ASCII;
    enum press_method pressMethod = COMPRESS_NONE;

    // Input arguments
    char *arg_dir_out = NULL;

    int opt;
    int longindex = 0;
    // Parse options
    while ((opt = getopt_long(argc, argv, "bchi:o:d:l", long_opts, &longindex)) != -1) {
        if (meta->debug) {
            DEBUG("opt='%c', optarg=\"%s\", optind=%d, opterr=%d, optopt='%c'",
                  opt, optarg, optind, opterr, optopt);
        }
        switch (opt) {
            case 'b':
                format_out = FORMAT_BINARY;
                break;
            case 'c':
                pressMethod = COMPRESS_GZIP;
                break;
            case 'l':
                lossy = 1;
                break;
            case 'h':
                if (meta->verbose) {
                    VERBOSE("displaying large help message%s","");
                }
                fprintf(stdout, HELP_LARGE_MSG, argv[0]);
                EXIT_MSG(EXIT_SUCCESS, argv, meta);
                return EXIT_SUCCESS;
            case 'd':
                arg_dir_out = optarg;
                break;
            case  0 :
                if (longindex == 4) {
                    iop = atoi(optarg);
                    if (iop < 1) {
                        ERROR("Number of I/O processes should be larger than 0. You entered %d", iop);
                        exit(EXIT_FAILURE);
                    }
                }
                break;
            default: // case '?'
                fprintf(stderr, HELP_SMALL_MSG, argv[0]);
                EXIT_MSG(EXIT_FAILURE, argv, meta);
                return EXIT_FAILURE;
        }
    }

    if(iop>1 && !arg_dir_out){
        ERROR("output directory should be specified when using multiprocessing iop=%d",iop);
        return EXIT_FAILURE;
    }

    // Check for remaining files to parse
    if (optind >= argc) {
        MESSAGE(stderr, "missing fast5 files or directories%s", "");
        fprintf(stderr, HELP_SMALL_MSG, argv[0]);
        EXIT_MSG(EXIT_FAILURE, argv, meta);
        return EXIT_FAILURE;
    }

    reads_count readsCount;
    std::vector<std::string> fast5_files;
    init_realtime = slow5_realtime();

    for (int i = optind; i < argc; ++ i) {
        if(iop==1){
            // Recursive way
            if(!arg_dir_out){
                WARNING("When converting multi-fast5 files with --iop=1 and -d=NULL, multiple headers will be written to stdout. It is recommended to set -d%s", ".");
            }
            recurse_dir(argv[i], format_out, pressMethod, lossy, &readsCount, arg_dir_out, meta);
        }else{
            find_all_5(argv[i], fast5_files, FAST5_EXTENSION);
        }
    }

    if(iop==1){
        MESSAGE(stderr, "total fast5: %lu, bad fast5: %lu", readsCount.total_5, readsCount.bad_5_file);
    }else{
        fprintf(stderr, "[%s] %ld fast5 files found - took %.3fs\n", __func__, fast5_files.size(), slow5_realtime() - init_realtime);
        f2s_iop(format_out, pressMethod, lossy, iop, fast5_files, arg_dir_out, meta, &readsCount);
    }


    EXIT_MSG(EXIT_SUCCESS, argv, meta);
    return EXIT_SUCCESS;
}
