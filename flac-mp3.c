/*
 * flac-mp3 - gst toy that decodes a flac and creates a mp3. woo!
 *
 * hrm. not quite working this... using idea from http://www.emcken.dk/weblog/
 *
 * Original author: Jonathan Landis <jjjhhhlll@gmail.com>
 */

#include <gst/gst.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define ENCODE_PROCESSES 3

static char *
tmp_filename(char *infile)
{
    char *tmp_file;
    char *tmp_dir = g_path_get_dirname(infile);
    char *tmp_base = g_path_get_basename(infile);

    tmp_file = g_strconcat(tmp_dir, "/.", tmp_base, NULL);
    g_free(tmp_dir);
    g_free(tmp_base);
    
    return tmp_file;
}

static gboolean
bus_call (GstBus *bus, GstMessage *msg, gpointer data)
{
    GMainLoop *loop = (GMainLoop *) data;

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_main_loop_quit(loop);
            break;
        
        case GST_MESSAGE_ERROR: {
            char *debug;
            GError *err;

            gst_message_parse_error(msg, &err, &debug);
            g_free(debug);

            fprintf(stderr, "error: %s\n", err->message);
            g_error_free(err);

            g_main_loop_quit(loop);
            break;
        }
        default: break;
    }

    return TRUE;
}

static void
encode_one_file(char *infile, char *outfile)
{
    GMainLoop *loop;
    GstElement *pipeline, *source, *decoder, *convert, *encoder, *tagger, *sink;
    GstBus *bus;
    char *tmp_file;


    loop = g_main_loop_new(NULL, FALSE);
    printf("writing %s\n", outfile);

    pipeline = gst_pipeline_new("flac-rip");
    source = gst_element_factory_make("filesrc", "src");
    decoder = gst_element_factory_make("flacdec", "flac");
    convert = gst_element_factory_make("audioconvert", "convert");
    encoder = gst_element_factory_make("lame", "mp3");
    tagger = gst_element_factory_make("id3v2mux", "tag");
    sink = gst_element_factory_make("filesink", "output");

    if (!(pipeline && source && decoder && convert && 
          encoder && tagger && sink)) {
        fprintf(stderr, "unable to make all necessary gstreamer elements\n");
        exit(1);
    }

    tmp_file = tmp_filename(outfile);

    g_object_set(G_OBJECT(source), "location", infile, NULL);
    g_object_set(G_OBJECT(sink), "location", tmp_file, NULL);
    g_object_set(G_OBJECT(encoder), "bitrate", 128, NULL);

    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);


    gst_bin_add_many(
        GST_BIN(pipeline), source, decoder, convert, encoder, tagger, sink, 
        NULL
    );
    gst_element_link_many(
        source, decoder, convert, encoder, tagger, sink, NULL
    );

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_main_loop_run(loop);

    g_rename(tmp_file, outfile);
    g_free(tmp_file);
}

static inline void
background_encode(char *infile, char *outfile, int *processes)
{
    if (*processes) {
again:
        (*processes)--;
        if (!fork()) {
            encode_one_file(infile, outfile);
            exit(0);
        }
    } else {
        int status;
        wait(&status);
        (*processes)++;
        goto again;
    }
}

static void 
encode_dir (char *fromdir, char *todir, int *processes) 
{
    GError *error;
    GDir* dh;
    const char *name;

    dh = g_dir_open(fromdir, 0, &error);

    while ((name = g_dir_read_name(dh))) {
        char *from_fullfile;
        char *to_fullfile;
        struct stat stats;

        if (g_str_has_prefix(name, ".")) continue;

        from_fullfile = g_strconcat(fromdir, "/", name, NULL); 
        to_fullfile = g_strconcat(todir, "/", name, NULL);

        g_stat(from_fullfile, &stats);

        if (S_ISDIR(stats.st_mode)) {
            encode_dir(from_fullfile, to_fullfile, processes);

        } else if (S_ISREG(stats.st_mode)) {
            if (g_str_has_suffix(name, ".flac")) {
                memcpy(rindex(to_fullfile, '.'), ".mp3\0", 5);

                if (g_stat(to_fullfile, &stats) == -1) {
                    g_mkdir_with_parents(todir, 0777);
                    background_encode(from_fullfile, to_fullfile, processes);
                }
            }
        }

        g_free(from_fullfile);
        g_free(to_fullfile);
    }

    g_dir_close(dh);
}


int
main (int argc, char *argv[])
{
    char *fromdir, *todir;
    int processes = ENCODE_PROCESSES;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <fromdir> <todir>\n", argv[0]);
        return -1;
    }

    fromdir = argv[1];
    todir = argv[2];

    gst_init(&argc, &argv);

    encode_dir(fromdir, todir, &processes);

    while (processes < ENCODE_PROCESSES) {
        int status;
        wait(&status);
        processes++;
    }

    return 0;
}
