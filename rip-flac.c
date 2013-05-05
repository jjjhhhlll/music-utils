/*
 * rip-flac - a cdrip program
 * does cd->flac->disk via gstreamer.
 *
 * I wanted to try out gstreamer and make things work
 * according to my liking.
 *
 * Original author: Jonathan Landis <jlandis@akamai.com>
 */

#include <string.h>
#include <stdlib.h>
#include <gst/gst.h>
#include <gst/tag/tag.h>
#include <glib/glist.h>
#include <glib/gstrfuncs.h>
#include <glib/gstdio.h>

#include <musicbrainz3/mb_c.h>

#include <sys/types.h> 
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/cdrom.h>
#include <unistd.h>

/* TODO: add a queue between source and encoder ? maybe will speed up 
 * this is what sound-juicer does... */

typedef struct {
    char *title;
    char *artist;
    char *artist_sortname;
    int num_tracks;
    int disc_number;
    GList * tracks;
    char *album_id;
    char *artist_id;
    GDate *release_date;
} album_info_t;

typedef struct {
    album_info_t *album;
    int track_num;
    char *title;
    char *artist;
    char *artist_sortname;
    int duration;
    char *track_id;
    char *artist_id;
} track_info_t;

static void
print_track_info (gpointer data, gpointer user_data)
{
    track_info_t *t = (track_info_t*)data;
    printf(
        "\t%d: %s [%d]\n",
        t->track_num, t->title, t->duration
    );
}

static void
print_album_info (album_info_t *a)
{
    printf(
        "title: %s\n"
        "artist: %s\n"
        "artist sort name: %s\n"
        "tracks: %d\n"
        "disc number: %d\n"
        "album id: %s\n"
        "artist id: %s\n",
        a->title, a->artist, a->artist_sortname, a->num_tracks, 
        a->disc_number, a->album_id, a->artist_id
    );

    g_list_foreach(a->tracks, print_track_info, 0);
}

static int
find_disc_num_and_offset(char *title, int *offset)
{
    static GRegex *disc_regex = NULL;
    GMatchInfo *info;
    int disc_number = 0;

    if (!disc_regex) {
        disc_regex = g_regex_new(".+( \\(disc (\\d+).*)", 0, 0, NULL);
    }

    if (g_regex_match(disc_regex, title, 0, &info)) {
        char *s;

        *offset = 0;

        g_match_info_fetch_pos(info, 1, offset, NULL);
        s = g_match_info_fetch(info, 2);
        disc_number = atoi(s);
        g_free(s);
    }
    g_match_info_free(info);
    return disc_number;
}

static GDate *
get_release_date(MbRelease release)
{
    char buf[256];
    int num_releases = mb_release_get_num_release_events(release);
    MbReleaseEvent evt;
    GDate *rv = NULL;

    if (num_releases <= 0) return NULL;

    evt = mb_release_get_release_event(release, 0);

    mb_release_event_get_date(evt, buf, sizeof(buf));
    {
        int matched, year=1, month=1, day=1;
        matched = sscanf(buf, "%u-%u-%u", &year, &month, &day);
        if (matched >= 1) {
            rv = g_date_new_dmy(
                day == 0 ? 1 : day,
                month == 0 ? 1 : month,
                year
            );
        }
    }
    return rv;
}

/*
static void
get_album(musicbrainz_t mb, char **album_id, char **title, int *disc_number)
{

    char buf[256];
    char *tmp_album;
    int discnum_offset = 0;

    if (mb_GetResultData(mb, MBE_AlbumGetAlbumId, buf, sizeof(buf))) {
        mb_GetIDFromURL(mb, buf, buf, sizeof(buf));
        *album_id = g_strdup(buf);
    }

    if (mb_GetResultData(mb, MBE_AlbumGetAlbumName, buf, sizeof(buf))) {
        tmp_album = buf;
    } else {
        tmp_album = "Unknown Title";
    }
    *disc_number = find_disc_num_and_offset(tmp_album, &discnum_offset);

    if (discnum_offset) {
        *title = g_strndup(tmp_album, discnum_offset);
    } else {
        *title = g_strdup(tmp_album);
    }
}
*/

static void
get_album(MbRelease release, char **album_id, char **title, int *disc_number)
{

    char buf[256];
    char *tmp_album;
    int discnum_offset = 0;

    mb_release_get_id(release, buf, sizeof(buf));
    *album_id = g_strdup(buf);

    mb_release_get_title(release, buf, sizeof(buf));
    tmp_album = buf;
    *disc_number = find_disc_num_and_offset(tmp_album, &discnum_offset);

    if (discnum_offset) {
        *title = g_strndup(tmp_album, discnum_offset);
    } else {
        *title = g_strdup(tmp_album);
    }
}

static void
get_album_artist(MbRelease release, char **artist_id, char **artist, 
                 char **artist_sort_name)
{
    char buf[256];
    MbArtist a;

    a = mb_release_get_artist(release);

    mb_artist_get_id(a, buf, sizeof(buf));
    *artist_id = g_strdup(buf);

    mb_artist_get_name(a, buf, sizeof(buf));
    *artist = g_strdup(buf);

    /* TODO: test various artists support */

    mb_artist_get_sortname(a, buf, sizeof(buf));
    *artist_sort_name = g_strdup(buf);

    /*
    char buf[256];
    char *tmp_artist;

    if (!mb_GetResultData(mb, MBE_AlbumGetAlbumArtistId, buf, sizeof(buf))) 
        return;

    mb_GetIDFromURL (mb, buf, buf, sizeof (buf));
    *artist_id = g_strdup(buf);

    if (mb_GetResultData(mb, MBE_AlbumGetAlbumArtistName, buf, sizeof(buf))) {
       tmp_artist = buf;

    } else {
        if (g_ascii_strcasecmp(MBI_VARIOUS_ARTIST_ID, *artist_id) == 0) {
            tmp_artist = "Various Artists";
        } else {
            tmp_artist = "Uknown Artist";
        }
    }
    *artist = g_strdup(tmp_artist);

    if (mb_GetResultData(mb, MBE_AlbumGetAlbumArtistSortName,
                         buf, sizeof(buf))) {
        *artist_sort_name = g_strdup(buf);
    }
    */
}


track_info_t *
get_one_track(MbRelease release, album_info_t *a, int i)
{
    MbTrack t;
    MbArtist artist;
    track_info_t *track; 
    char buf[256];

    track = g_new0(track_info_t, 1);
    track->album = a;

    track->track_num = i;

    t = mb_release_get_track(release, i-1);

    mb_track_get_id(t, buf, sizeof(buf));
    track->track_id = g_strdup(buf);

    mb_track_get_title(t, buf, sizeof(buf));
    track->title = g_strdup(buf);
    // TODO: handle [Untitled]
    
    artist = mb_track_get_artist(t);
    if (!artist)
        artist = mb_release_get_artist(release);

    mb_artist_get_name(artist, buf, sizeof(buf));
    track->artist = g_strdup(buf);

    mb_artist_get_sortname(artist, buf, sizeof(buf));
    track->artist_sortname = g_strdup(buf);

    track->duration = mb_track_get_duration(t) / 1000;

    return track;


    /*
    track_info_t *track; 
    char buf[256];

    track = g_new0(track_info_t, 1);
    track->album = a;

    track->track_num = i;

    if (mb_GetResultData1(mb, MBE_AlbumGetTrackId, buf, sizeof(buf), i)) {
        mb_GetIDFromURL(mb, buf, buf, sizeof(buf));
        track->track_id = g_strdup(buf);
    }

    if (mb_GetResultData1(mb, MBE_AlbumGetArtistId, buf, sizeof(buf), i)) {
        mb_GetIDFromURL(mb, buf, buf, sizeof(buf));
        track->artist_id = g_strdup(buf);
    }

    if (mb_GetResultData1(mb, MBE_AlbumGetTrackName, buf, sizeof(buf), i)) {
        track->title = g_strdup(buf);
    } else {
        track->title = g_strdup("[Untitled]");
    }

    if (track->artist == NULL &&
        mb_GetResultData1(mb, MBE_AlbumGetArtistName, buf, sizeof(buf), i)) {
        track->artist = g_strdup(buf);
    }

    if (mb_GetResultData1(mb, MBE_AlbumGetArtistSortName, 
                          buf, sizeof(buf), i)) {

        track->artist_sortname = g_strdup(buf);
    }

    if (mb_GetResultData1(mb, MBE_AlbumGetTrackDuration, buf, sizeof(buf), i)) {
        track->duration = atoi(buf) / 1000;
    }

    return track;
    */
}


static void
get_tracks(MbRelease release, album_info_t *album)
{
    int i;

    album->num_tracks = mb_release_get_num_tracks(release);
    if (album->num_tracks < 1) {
        fprintf(stderr, "hrm. no tracks. odd\n");
        return;
    }

    album->tracks = NULL;

    for (i=1; i <= album->num_tracks; i++) {
        track_info_t *track = get_one_track(release, album, i);
        album->tracks = g_list_append(album->tracks, track);
    }

    /*
    album->num_tracks = mb_GetResultInt(mb, MBE_AlbumGetNumTracks);
    if (album->num_tracks < 1) {
        fprintf(stderr, "hrm. no tracks. odd\n");
        return;
    }

    album->tracks = NULL;

    for (i=1; i <= album->num_tracks; i++) {
        track_info_t *track = get_one_track(mb, album, i);
        album->tracks = g_list_append(album->tracks, track);
    }
    */
}

static void
lookup_cd_info (char *device, album_info_t *album)
{
    MbDisc disc;
    MbQuery q;
    MbReleaseFilter f;
    MbResultList results;
    MbRelease release;
    char discid[100];
    int num_albums;

    memset(album, 1, sizeof(*album));

    disc = mb_read_disc(device);
    if (!disc) {
        fprintf(stderr, "unable to identify disc.\n");
        exit(1);
    }

    mb_disc_get_id(disc, discid, 100);
    fprintf(stderr, "got disc id: %s\n\n", discid);

    q = mb_query_new(NULL, NULL);
    f = mb_release_filter_new();

    mb_release_filter_disc_id(f, discid);
    results = mb_query_get_releases(q,f);

    mb_release_filter_free(f);
    mb_query_free(q);

    if (!results) {
        fprintf(stderr, "no results found\n");
        exit(1);
    }

    num_albums = mb_result_list_get_size(results);
    if (num_albums < 1) {
        char buf[256];
        mb_get_submission_url(disc, 0, 0, buf, sizeof(buf));
        fprintf(stderr, "nothing in musicbrainz for this one. add it via: %s\n", buf);
        exit(1);
    }

    if (num_albums > 1) {
        fprintf(stderr, "multiple albums!!! picking the last one\n");
    }

    release = mb_result_list_get_release(results, num_albums-1);
    get_album(release, &album->album_id, &album->title, &album->disc_number);

    album->release_date = get_release_date(release);

    get_album_artist(
        release, &album->artist_id, &album->artist, &album->artist_sortname
    );

    get_tracks(release, album);


    /*
    musicbrainz_t mb;
    char buf[256];
    int num_albums;

    mb = mb_New();
    mb_SetDevice(mb, device);
    mb_UseUTF8(mb, TRUE);

#ifdef MB_DEBUG
    mb_SetDebug(mb, TRUE);
#endif

    memset(album, 1, sizeof(*album));
    if (!mb_Query(mb, MBQ_GetCDInfo)) {
        mb_GetQueryError(mb, buf, sizeof(buf));
        fprintf(stderr, "unable to query cd: %s\n", buf);
        exit(1);
    }

    num_albums = mb_GetResultInt(mb, MBE_GetNumAlbums);

    if (num_albums < 1) {
        fprintf(stderr, "nothing in musicbrainz for this one. TODO add url\n");
        exit(1);
    }

    if (num_albums > 1) {
        fprintf(stderr, "multiple albums!!! picking the last one\n");
    }

    mb_Select1(mb, MBS_SelectAlbum, num_albums);

    get_album(mb, &album->album_id, &album->title, &album->disc_number);

    album->release_date = get_release_date(mb);
    
    get_album_artist(
        mb, &album->artist_id, &album->artist, &album->artist_sortname
    );

    get_tracks(mb, album);

    mb_Delete(mb);
    */
}

static void
set_tags (GstElement *pipeline, track_info_t *track)
{
    GstIterator *iter;
    GstTagSetter *tagger;
    gboolean done = FALSE;

    iter = gst_bin_iterate_all_by_interface(
        GST_BIN(pipeline), GST_TYPE_TAG_SETTER
    );

    while (!done) {
        switch (gst_iterator_next(iter, (gpointer)&tagger)) {
        case GST_ITERATOR_OK:
            gst_tag_setter_add_tags(
                tagger,
                GST_TAG_MERGE_REPLACE,
                GST_TAG_TITLE, track->title,
                GST_TAG_ARTIST, track->artist,
                GST_TAG_TRACK_NUMBER, track->track_num,
                GST_TAG_ALBUM, track->album->title,
                GST_TAG_DURATION, track->duration *GST_SECOND,
                GST_TAG_MUSICBRAINZ_ALBUMID, track->album->album_id,
                GST_TAG_MUSICBRAINZ_ALBUMARTISTID, track->album->artist_id,
                GST_TAG_MUSICBRAINZ_ARTISTID, track->artist_id,
                GST_TAG_MUSICBRAINZ_TRACKID, track->track_id,
                GST_TAG_MUSICBRAINZ_SORTNAME, track->artist_sortname,
                NULL
            );

            if (track->album->disc_number > 0) {
                gst_tag_setter_add_tags(
                    tagger,
                    GST_TAG_MERGE_APPEND,
                    GST_TAG_ALBUM_VOLUME_NUMBER, track->album->disc_number,
                    NULL
                );
            }

            if (track->album->release_date) {
                gst_tag_setter_add_tags(
                    tagger,
                    GST_TAG_MERGE_APPEND,
                    GST_TAG_DATE, track->album->release_date,
                    NULL
                );
            }
            break;

        case GST_ITERATOR_RESYNC:
            fprintf(stderr, "got iterator resync. wtf\n");
            gst_iterator_resync(iter);
            break;

        case GST_ITERATOR_ERROR:
            fprintf(stderr, "iterator error. wtf\n");
            /* falling */ 
        case GST_ITERATOR_DONE:
            done = TRUE;
            break;
        }
    }
    gst_iterator_free(iter);
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

static char *
escape(char *str)
{
    gchar *res = NULL;
    gchar *s;

    s = g_strdup(str);
    g_strdelimit(s, "/", '-'); /* remove slashes */
    res = g_filename_from_utf8(s, -1, NULL, NULL, NULL);
    g_free(s);

    return res ? res : g_strdup(str);
}

static char *
track_filename(track_info_t *track)
{
    char buf[4096];
    // TODO: various artist support
    int is_various_artists = 0;
    /*
    int is_various_artists = g_ascii_strcasecmp(
        MBI_VARIOUS_ARTIST_ID, track->album->artist_id
    ) == 0;
    */

    if (is_various_artists) {
         if (track->album->disc_number > 0) {
            snprintf(
                buf, 4096, "%s/Music/Various Artists/%s/%d%02d-%s-%s.flac", 
                getenv("HOME"),
                escape(track->album->title), track->album->disc_number,
                track->track_num,
                escape(track->artist), escape(track->title)
            );
        } else {
            snprintf(
                buf, 4096, "%s/Music/Various Artists/%s/%02d-%s-%s.flac", 
                getenv("HOME"),
                escape(track->album->title), 
                track->track_num, 
                escape(track->artist),
                escape(track->title)
            );
        }
    } else {
        if (track->album->disc_number > 0) {
            snprintf(
                buf, 4096, "%s/Music/%s/%s/%d%02d-%s.flac", 
                getenv("HOME"),
                escape(track->artist), 
                escape(track->album->title), track->album->disc_number,
                track->track_num, escape(track->title)
            );
        } else {
            snprintf(
                buf, 4096, "%s/Music/%s/%s/%02d-%s.flac", 
                getenv("HOME"),
                escape(track->artist), escape(track->album->title), 
                track->track_num, 
                escape(track->title)
            );
        }
    }

    return g_strdup(buf);
}

static void
rip_one_track(gpointer data, gpointer user_data)
{
    track_info_t *track = (track_info_t*)data;
    char *cdrom_device = (char*)user_data;
    GstBus *bus;
    GstElement *pipeline, *source, *encoder, *sink;
    GMainLoop *loop;
    char *tmp_file, *final_file;


    loop = g_main_loop_new(NULL, FALSE);

    pipeline = gst_pipeline_new("rip-flac");
    source = gst_element_factory_make("cdparanoiasrc", "rip");
    encoder = gst_element_factory_make("flacenc", "flac");
    sink = gst_element_factory_make("filesink", "output");

    if (!(pipeline && source && encoder && sink)) {
        fprintf(stderr, "unable to make all necessary gstreamer elements\n");
        return;
    }

    g_object_set(G_OBJECT(source), "device", cdrom_device, NULL);
    g_object_set(G_OBJECT(source), "track", track->track_num, NULL);

    g_object_set(G_OBJECT(encoder), "quality", 8, NULL);

    final_file = track_filename(track);
    {
        char *tmp_dir = g_path_get_dirname(final_file);
        char *tmp_base = g_path_get_basename(final_file);

        tmp_file = g_strconcat(tmp_dir, "/.", tmp_base, NULL);

        g_mkdir_with_parents(tmp_dir, 0755);

        g_free(tmp_dir);
        g_free(tmp_base);
    }
    g_object_set(G_OBJECT(sink), "location", tmp_file, NULL);

    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    gst_bin_add_many(GST_BIN(pipeline), source, encoder, sink, NULL);

    gst_element_link_many(source, encoder, sink, NULL);

    set_tags(pipeline, track);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    printf("ripping track: %d\n", track->track_num);
    g_main_loop_run(loop);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(GST_OBJECT(pipeline));

    g_rename(tmp_file, final_file);

    g_free(tmp_file);
    g_free(final_file);
}

static void
eject_cdrom(char *device)
{
    int fd;

    fd = open(device, O_RDONLY | O_NONBLOCK);
    ioctl(fd, CDROMEJECT, 0);
    close(fd);
}

int
main (int argc, char *argv[])
{
    char *cdrom_device;
    album_info_t album_data;
    
    gst_init(&argc, &argv);


    if (argc != 2) {
        printf("usage: %s <cdrom device>\n", argv[0]);
        return -1;
    }

    cdrom_device = argv[1];
    lookup_cd_info(cdrom_device, &album_data);
    print_album_info(&album_data);

    g_list_foreach(album_data.tracks, rip_one_track, cdrom_device);
    eject_cdrom(cdrom_device);

    return 0;
}
