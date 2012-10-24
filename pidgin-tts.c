/*
 * File:        pidgin-tts.c
 * Author:      Thomas Gläßle
 * Version:     1.1
 * License:     free
 *
 * Description:
 * Source file for text-to-speech (tts) plugin for pidgin.
 * Designed for use with the `espeak` utility.
 *
 * Installation:
 * May be as simple as typing
 *  make && make install
 * and activating the plugin in your pidgin options.
 */

# ifdef _WIN32
#   error "This will probably not work on Windows!"
# endif

// Prerequisites {{{1
# ifndef G_GNUC_NULL_TERMINATED
#   if __GNUC__ >= 4
#     define G_GNUC_NULL_TERMINATED __attribute__((__sentinel__))
#   else
#     define G_GNUC_NULL_TERMINATED
#   endif /* __GNUC__ >= 4 */
# endif /* G_GNUC_NULL_TERMINATED */

# define PURPLE_PLUGINS

// purple includes {{{2
# include <pidgin/gtkplugin.h>       /* gtk stuff */
# include <libpurple/cmds.h>         /* purple_cmd_xxx */
# include <libpurple/conversation.h> /* purple_conversation_xxx */
# include <libpurple/debug.h>        /* purple_debug_xxx */
# include <libpurple/prefs.h>        /* purple_pref_xxx */
# include <libpurple/signals.h>      /* purple_signal_xxx, ... */
# include <libpurple/util.h>         /* purple_str_xxx */
# include <libpurple/version.h>

// system includes {{{2
# include <stdio.h>
# include <stdarg.h>     /* va_list ... */
# include <string.h>
# include <unistd.h>     /* write, close */
# include <errno.h>
# include <sys/types.h>

// global constants {{{2
# define PLUGIN_ID      "qjuh-pidgin-tts"
# define PLUGIN_NAME    "Pidgin-eSpeak"

# define PREFS_BASE     "/plugins/core/pidgin-tts"
# define PREFS_ACTIVE   PREFS_BASE "/active"
# define PREFS_REPLACE  PREFS_BASE "/replace"
# define PREFS_SHELL    PREFS_BASE "/shell"
# define PREFS_COMMAND  PREFS_BASE "/command"
# define PREFS_PARAM    PREFS_BASE "/params"
# define PREFS_KEYWORDS PREFS_BASE "/keywords"
# define PREFS_KEYS_ON  PREFS_BASE "/keywords-active"

# define PREFS_LANGUAGE PREFS_BASE "/language"
# define PREFS_VOLUME   PREFS_BASE "/volume"

# define DEFAULT_LANGUAGE   "de"
# define DEFAULT_VOLUME     "200"
# define DEFAULT_SHELL      "/bin/sh"
# define DEFAULT_TTS        "/usr/bin/espeak"

# define CMD_TTS "tts"

// forward declarations {{{2
static void plugin_init(PurplePlugin *plugin);
static gboolean plugin_load(PurplePlugin *plugin);
static gboolean plugin_unload(PurplePlugin * plugin);

// instance variables {{{2
static PurplePlugin *me;
static int tts_queue_stdin, tts_queue_pid,
           tts_command_id_global, tts_command_id_conversation,
           tts_command_id_keyword, tts_command_id_replace;
static GList* active_conversations;
static GList* inactive_conversations;

// Export plugin {{{1
static PurplePluginInfo pluginInfo =
{
    PURPLE_PLUGIN_MAGIC,
    PURPLE_MAJOR_VERSION,
    PURPLE_MINOR_VERSION,
    PURPLE_PLUGIN_STANDARD,                             // type
    PIDGIN_PLUGIN_TYPE,                                 // ui_requirement
    0,                                                  // flags
    NULL,                                               // dependencies
    PURPLE_PRIORITY_DEFAULT,                            // priority

    PLUGIN_ID,                                          // id
    PLUGIN_NAME,                                        // name
    "1.1",                                              // version
    "Read incoming text messages.",                     // summary
    "This plugin speaks out all incoming text messages via espeak.",
    "Thomas Gläßle <t_glaessle@gmx.de>",                // author
    "https://github.com/thomas-glaessle/pidgin-tts",    // homepage
    plugin_load,                                        // load
    plugin_unload,                                      // unload
    NULL,                                               // destroy
    NULL,                                               // ui_info
    NULL,                                               // extra_info
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

PURPLE_INIT_PLUGIN(PidginEspeak, plugin_init, pluginInfo)


// Utility functions {{{1
void systemlog(PurpleConversation *conv, const gchar* format, ...) __attribute__((format(printf,2,3)));
void systemlog(PurpleConversation *conv, const gchar* format, ...)
{
    va_list ap;
    gchar* str;

    va_start(ap, format);
    str = g_strdup_vprintf(format, ap);
    va_end(ap);

    purple_conversation_write(
        conv, NULL, str,
        PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG | PURPLE_MESSAGE_ACTIVE_ONLY,
        0); /* time(NULL)); */ 

    g_free(str);
}

int strwrite(int fd, ...) __attribute__((__sentinel__(0)));
int strwrite(int fd, ...)
{
    int written, c;
    gchar* str;
    va_list ap;

    written = 0;
    va_start(ap, fd);
    while (str = va_arg(ap, gchar*)) {
        c = write(fd, str, strlen(str));
        if (c < 0)
            return c;
        written += c;
    }
    va_end(ap);
    return written;
}

GList* list_find(GList* list, const gchar* str, int step)
{
    for ( ; list; list = g_list_nth(list, step))
        if (purple_strequal(g_list_nth_data(list, 0), str))
            break;
    return list;
}

// spawn a process
GPid spawn(const gchar *cmd, const gchar *opts[], int copts, int *infp, int *outfp)
{
    int i;
    gchar **opt;
    GPid pid = 0;

    // create argv
    opt = malloc((copts+2)*sizeof(gchar*));
    opt[0] = g_strdup(cmd);
    for (i = 0; i < copts; i++)
        opt[i+1] = g_strdup(opts[i]);
    opt[copts+1] = NULL;

    g_spawn_async_with_pipes(
            NULL,           // inherit current working directory
            opt,            // argv
            NULL,           // envp
            G_SPAWN_STDOUT_TO_DEV_NULL|G_SPAWN_STDERR_TO_DEV_NULL,
            NULL,           // SetupFunction
            NULL,           // USERDATA
            &pid,           // child PID
            infp,           // child's STDIN
            outfp,          // child's STDOUT
            NULL,           // child's STDERR
            NULL            // error
        );

    for (i = 0; i < copts; ++i)
        g_free(opt[i+1]);
    free(opt);

    return pid;
}

// Global preferences {{{1

/* getters {{{2 */
static gboolean pref_get_active()
{
    return purple_prefs_get_bool(PREFS_ACTIVE);
} 

static const gchar* pref_get_command()
{
    return purple_prefs_get_string(PREFS_COMMAND);
} 

static const gchar* pref_get_shell()
{
    return purple_prefs_get_string(PREFS_SHELL);
}

static const gchar* pref_get_param()
{
    return purple_prefs_get_string(PREFS_PARAM);
}

static gboolean pref_get_keywords_active()
{
    return purple_prefs_get_bool(PREFS_KEYS_ON);
}

/* setters {{{2 */
static void pref_set_active(gboolean active)
{
    purple_prefs_set_bool(PREFS_ACTIVE, active);
}

static void pref_set_command(const gchar* cmd)
{
    purple_prefs_set_string(PREFS_COMMAND, cmd);
}

static void pref_set_shell(const gchar* sh)
{
    purple_prefs_set_string(PREFS_SHELL, sh);
}

static void pref_set_param(const gchar* param)
{
    purple_prefs_set_string(PREFS_PARAM, param);
}

static void pref_set_keywords_active(gboolean active)
{
    purple_prefs_set_bool(PREFS_KEYS_ON, active);
}

/* logging {{{2 */
static void pref_log_active(PurpleConversation *conv)
{
    systemlog(conv, PLUGIN_NAME " is %s", (pref_get_active() ? "enabled" : "disabled"));
}

static void pref_log_shell(PurpleConversation *conv)
{
    systemlog(conv, PLUGIN_NAME " shell is: %s", pref_get_shell());
}

static void pref_log_command(PurpleConversation *conv)
{
    systemlog(conv, PLUGIN_NAME " command is: %s", pref_get_command());
}

static void pref_log_param(PurpleConversation *conv)
{
    systemlog(conv, PLUGIN_NAME " parameters are: %s", pref_get_param());
}

static void pref_log_keywords_active(PurpleConversation *conv)
{
    systemlog(conv, PLUGIN_NAME " keywords are: %s", (pref_get_keywords_active() ? "enabled" : "disabled"));
}


// Keyword management {{{1
static void pref_delete_keyword(const gchar* keyword)
{
    GList *table = purple_prefs_get_string_list(PREFS_KEYWORDS),
          *match = list_find(table, keyword, 1);
    if (match != NULL) {
        g_free(g_list_nth_data(match, 0));
        table = g_list_delete_link(table, match);
        purple_prefs_set_string_list(PREFS_KEYWORDS, table);
    }
}

static void pref_add_keyword(const gchar* keyword)
{
    GList *table = purple_prefs_get_string_list(PREFS_KEYWORDS),
          *match = list_find(table, keyword, 1);
    if (match == NULL) {
        table = g_list_prepend(table, g_strdup(keyword));
        purple_prefs_set_string_list(PREFS_KEYWORDS, table);
    }
}

static void pref_log_keywords(PurpleConversation *conv)
{
    gchar *tmp, *str;
    GList *table = purple_prefs_get_string_list(PREFS_KEYWORDS);
    table = g_list_first(table);

    if (table == NULL)
        str = g_strdup(PLUGIN_NAME " active keywords: (none)");
    else { 
        str = g_strjoin("", PLUGIN_NAME " active keywords: ", g_list_nth_data(table, 0), NULL);
        for (table = g_list_next(table); table != NULL; table = g_list_next(table)) {
            tmp = g_strjoin("", str, ", ", g_list_nth_data(table, 0), NULL);
            g_free(str);
            str = tmp;
        }
    }

    systemlog(conv, "%s", str);
    g_free(str);
}


// Replacement table {{{1
static void pref_delete_replace(const gchar* pattern)
{
    GList *table = purple_prefs_get_string_list(PREFS_REPLACE),
          *match = list_find(table, pattern, 2);
    if (match != NULL) {
        g_free(g_list_nth_data(match, 1));
        g_free(g_list_nth_data(match, 0));
        table = g_list_delete_link(table, g_list_nth(match, 1));
        table = g_list_delete_link(table, g_list_nth(match, 0));
        purple_prefs_set_string_list(PREFS_REPLACE, table);
    }
}

static void pref_add_replace(const gchar* pattern, const gchar* replace)
{
    GList *table;
    pref_delete_replace(pattern);
    table = purple_prefs_get_string_list(PREFS_REPLACE);
    table = g_list_prepend(table, g_strdup(replace)); 
    table = g_list_prepend(table, g_strdup(pattern)); 
    purple_prefs_set_string_list(PREFS_REPLACE, table);
}

static void pref_log_replace(PurpleConversation *conv)
{
    gchar *tmp, *str = g_strdup(PLUGIN_NAME " active replacements:");
    GList *table = purple_prefs_get_string_list(PREFS_REPLACE);

    for (table = g_list_first(table); table != NULL; table = g_list_nth(table, 2)) {
        tmp = g_strjoin("", str, "\n", (const gchar*) g_list_nth_data(table, 0), " => ", (const gchar*) g_list_nth_data(table, 1), NULL);
        g_free(str);
        str = tmp;
    }

    systemlog(conv, "%s", str);
    g_free(str);
}

// Conversation preferences {{{1
/* getters {{{2 */
static gboolean conv_get_active(PurpleConversation *conv)
{
    return g_list_find(active_conversations, conv) != NULL;
} 

static gboolean conv_get_inactive(PurpleConversation *conv)
{
    return g_list_find(inactive_conversations, conv) != NULL;
} 

/* setters {{{2 */
static void conv_set_active(PurpleConversation *conv, gboolean active);
static void conv_set_inactive(PurpleConversation *conv, gboolean inactive);

static void conv_set_active(PurpleConversation *conv, gboolean active)
{
    if (active) {
        active_conversations = g_list_prepend(active_conversations, conv);
        conv_set_inactive(conv, FALSE);
    }
    else
        active_conversations = g_list_remove(active_conversations, conv);
}

static void conv_set_inactive(PurpleConversation *conv, gboolean inactive)
{
    if (inactive) {
        inactive_conversations = g_list_prepend(inactive_conversations, conv);
        conv_set_active(conv, FALSE);
    }
    else
        inactive_conversations = g_list_remove(inactive_conversations, conv);
}

/* logging {{{2 */
static void conv_log_active(PurpleConversation *conv) {
    if (conv_get_active(conv) || conv_get_inactive(conv))
        systemlog(
            conv, PLUGIN_NAME " is %s for this conversation",
            (conv_get_active(conv) ? "enabled" : "disabled"));
    else
        systemlog(
            conv, PLUGIN_NAME " uses the default setting (%s) for this conversation", 
            (pref_get_active() ? "enabled" : "disabled"));
}

// Business logic {{{1
// analyse message text {{{2
static gboolean analyse(const gchar* _buffer, gchar **text)
{
    GList* table;
    gchar *buffer, *tmpbuffer;
    
    // copy buffer and remove <html-tags>, apostrophes \', and newlines \n
    buffer = purple_markup_strip_html(_buffer);
    purple_str_strip_char(buffer, '\'');
    purple_str_strip_char(buffer, '\n');

    // replace
    table = purple_prefs_get_string_list(PREFS_REPLACE);

    for (table = g_list_first(table); table != NULL; table = g_list_nth(table, 2)) {
        tmpbuffer = purple_strreplace(buffer, g_list_nth_data(table, 0), g_list_nth_data(table, 1));
        g_free(buffer);
        buffer = tmpbuffer;
    }

    *text = buffer;
    return TRUE;
}

// execute espeak {{{2
static gboolean tts(PurpleConversation *conv, gchar *message)
{
    purple_debug_info(PLUGIN_NAME, "Echoing: '%s'\n", message);

    int written = strwrite(tts_queue_stdin,
        pref_get_command(), " ", pref_get_param(), " '", message, "'\n", NULL);

    if (written < 0) {
        purple_debug_error(PLUGIN_NAME, "Error while executing %s: '%s'\n", pref_get_command(), strerror(errno));
        return FALSE;
    }

    return TRUE;
}

// incoming message {{{2
static gboolean process_message(PurpleConversation *conv, const gchar* message)
{
    gchar* text;
    GList* keywords;
    gboolean keyword_found = FALSE;

    if (conv_get_inactive(conv))
        return FALSE;

    if (!conv_get_active(conv) && !pref_get_active()) {
        if (pref_get_keywords_active()) {
            keywords = purple_prefs_get_string_list(PREFS_KEYWORDS);
            for (keywords = g_list_first(keywords); keywords; keywords = g_list_next(keywords)) {
                if (g_strstr_len(message, -1, g_list_nth_data(keywords, 0)) != NULL) {
                    keyword_found = TRUE;
                    break;
                }
            }
        }
        if (!keyword_found)
            return FALSE;
    }
    if (!analyse(message, &text))
        return FALSE;

    tts(conv, text);
    g_free(text);
    return TRUE;
}

static gboolean message_receive(PurpleAccount *account, const gchar *who, gchar *message, PurpleConversation *conv, PurpleMessageFlags flags)
{
    process_message(conv, message);
    return FALSE;
}


// CLI {{{1
static PurpleCmdRet tts_command_keyword(
        PurpleConversation *conv,
        const gchar *cmd,
        gchar **args,
        gchar **error,
        void *data)
{
    if (args[0] == NULL || !purple_strequal(args[0], "keyword"))
        return PURPLE_CMD_RET_CONTINUE;

    else if (args[1] == NULL)   // list all keywords
        pref_log_keywords_active(conv);

    else if (args[2] == NULL)
    {
        if (purple_strequal(args[1], "on")) {
            pref_set_keywords_active(TRUE);
            pref_log_keywords_active(conv);
        }
        else if (purple_strequal(args[1], "off")) {
            pref_set_keywords_active(FALSE);
            pref_log_keywords_active(conv);
        }

        else if (purple_strequal(args[1], "list"))
            pref_log_keywords(conv);

        else
            return PURPLE_CMD_RET_FAILED;
    }
    else
    {
        if (purple_strequal(args[1], "add"))
            pref_add_keyword(args[2]);

        else if (purple_strequal(args[1], "remove"))
            pref_delete_keyword(args[2]);

        else
            return PURPLE_CMD_RET_FAILED;
    }

    return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet tts_command_replace(
        PurpleConversation *conv,
        const gchar *cmd,
        gchar **args,
        gchar **error,
        void *data)
{
    if (args[0] == NULL || !purple_strequal(args[0], "replace"))
        return PURPLE_CMD_RET_CONTINUE;

    if (args[1] == NULL)
        pref_log_replace(conv);
    else if (args[2] == NULL) {
        pref_delete_replace(args[1]);
        systemlog(conv, PLUGIN_NAME " - deleted replacement for: %s", args[1]);
    }
    else {
        pref_add_replace(args[1], args[2]);
        systemlog(conv, PLUGIN_NAME " - added replacement for: %s", args[1]);
    }

    return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet tts_command_conv(
        PurpleConversation *conv,
        const gchar *cmd,
        gchar **args,
        gchar **error,
        void *data)
{
    if (args[0] == NULL || !purple_strequal(args[0], "buddy"))
        return PURPLE_CMD_RET_CONTINUE;

    if (args[1] == NULL)
        conv_log_active(conv);

    else if (purple_strequal(args[1], "on")) {
        if (conv_get_inactive(conv) && pref_get_active())
            conv_set_inactive(conv, FALSE);
        else
            conv_set_active(conv, TRUE);
        conv_log_active(conv);
    }

    else if (purple_strequal(args[1], "off")) {
        if (conv_get_active(conv) && !pref_get_active())
            conv_set_active(conv, FALSE);
        else
            conv_set_inactive(conv, TRUE);
        conv_log_active(conv);
    }

    else
        return PURPLE_CMD_RET_FAILED;

    return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet tts_command(
        PurpleConversation *conv,
        const gchar *cmd,
        gchar **args,
        gchar **error,
        void *data)
{
    if (args[0] == NULL) {
        pref_log_active(conv);
        conv_log_active(conv);
    }

    else
    {
        if (args[1] == NULL)
        {
            if (purple_strequal(args[0], "on")) {
                pref_set_active(TRUE);
                conv_set_inactive(conv, FALSE);
                pref_log_active(conv);
            }

            else if (purple_strequal(args[0], "off")) {
                pref_set_active(FALSE);
                conv_set_active(conv, FALSE);
                pref_log_active(conv);
            }

            else if (purple_strequal(args[0], "shell"))
                pref_log_shell(conv);

            else if (purple_strequal(args[0], "command"))
                pref_log_command(conv);

            else if (purple_strequal(args[0], "param"))
                pref_log_param(conv);

            else if (purple_strequal(args[0], "status")) {
                pref_log_active(conv);
                conv_log_active(conv);
                pref_log_shell(conv);
                pref_log_command(conv);
                pref_log_param(conv);
                pref_log_keywords_active(conv);
                pref_log_keywords(conv);
                pref_log_replace(conv);
            }

            else
                return PURPLE_CMD_RET_FAILED;
        }
        else
        {
            if (purple_strequal(args[0], "shell")) {
                pref_set_shell(args[1]);
                pref_log_shell(conv);
            }

            else if (purple_strequal(args[0], "command")) {
                pref_set_command(args[1]);
                pref_log_command(conv);
            }

            else if (purple_strequal(args[0], "param")) {
                pref_set_param(args[1]);
                pref_log_param(conv);
            }

            else if (purple_strequal(args[0], "say")) {
                gchar* text;
                if (analyse(args[1], &text)) {
                    tts(conv, text);
                    g_free(text);
                }
            }

            else if (purple_strequal(args[0], "test")) {
                if (process_message(conv, args[1])) 
                    systemlog(conv, PLUGIN_NAME " - echoing test string...");
                else
                    systemlog(conv, PLUGIN_NAME " - not echoing test string");
            }

            else
                return PURPLE_CMD_RET_FAILED;
        }
    }
    return PURPLE_CMD_RET_OK;
}



// Initialization {{{1
static void plugin_init(PurplePlugin *plugin)
{
    purple_prefs_add_none(PREFS_BASE);
    purple_prefs_add_bool(PREFS_ACTIVE, FALSE);
    purple_prefs_add_string(PREFS_PARAM, "-v de -a 200");

    purple_prefs_add_string(PREFS_LANGUAGE, DEFAULT_LANGUAGE);
    purple_prefs_add_string(PREFS_VOLUME,   DEFAULT_VOLUME);

    purple_prefs_add_string_list(PREFS_REPLACE, NULL);
    purple_prefs_add_string_list(PREFS_KEYWORDS, NULL);
    purple_prefs_add_bool(PREFS_KEYS_ON, FALSE);

    purple_prefs_add_string(PREFS_SHELL,   DEFAULT_SHELL);
    purple_prefs_add_string(PREFS_COMMAND, DEFAULT_TTS);
}

static gboolean plugin_load(PurplePlugin *plugin)
{
    void *conv_handle = purple_conversations_get_handle();
    gchar
        *info_keyword = "/"CMD_TTS" keyword [add &lt;keyword&gt; | remove &lt;keyword&gt;]",
        *info_replace = "/"CMD_TTS" replace &lt;word&gt; &lt;replacement&gt;",
        *info = "/"CMD_TTS" [on | off | param &lt;parameters&gt; | shell &lt;path&gt; | command &lt;path&gt; | say &lt;text&gt; | status]",
        *info_stts = "/"CMD_TTS" buddy [on | off]";

    PurpleCmdFlag flags =
        PURPLE_CMD_FLAG_IM | PURPLE_CMD_FLAG_CHAT
        | PURPLE_CMD_FLAG_ALLOW_WRONG_ARGS;


    me = plugin;

    // start child process
    tts_queue_pid = spawn(pref_get_shell(), NULL, 0, &tts_queue_stdin, NULL);
 
    // register command handlers
    tts_command_id_global = purple_cmd_register(
            CMD_TTS,                              /* command name */ 
            "ws",                               /* command argument format */
            PURPLE_CMD_P_DEFAULT,               /* command priority flags */  
            flags,                              /* command usage flags */
            PLUGIN_ID,                          /* Plugin ID */
            tts_command,                        /* Name of the callback function */
            info,                               /* Help message */
            NULL );                             /* Any special user-defined data */
    tts_command_id_conversation = purple_cmd_register(
            CMD_TTS,                              /* command name: stts = single tts */ 
            "wws",                              /* command argument format */
            PURPLE_CMD_P_DEFAULT,               /* command priority flags */  
            flags,                              /* command usage flags */
            PLUGIN_ID,                          /* Plugin ID */
            tts_command_conv,                   /* Name of the callback function */
            info_stts,                          /* Help message */
            NULL );                             /* Any special user-defined data */
    tts_command_id_keyword = purple_cmd_register(
            CMD_TTS,                              /* command name */ 
            "wws",                              /* command argument format */
            PURPLE_CMD_P_DEFAULT,               /* command priority flags */  
            flags,                              /* command usage flags */
            PLUGIN_ID,                          /* Plugin ID */
            tts_command_keyword,                /* Name of the callback function */
            info_keyword,                       /* Help message */
            NULL );                             /* Any special user-defined data */
    tts_command_id_replace = purple_cmd_register(
            CMD_TTS,                              /* command name */ 
            "wws",                              /* command argument format */
            PURPLE_CMD_P_DEFAULT,               /* command priority flags */  
            flags,                              /* command usage flags */
            PLUGIN_ID,                          /* Plugin ID */
            tts_command_replace,                /* Name of the callback function */
            info_replace,                       /* Help message */
            NULL );                             /* Any special user-defined data */


    // TODO: add commands to show/edit replacement table !!
    // TODO: add command to stop espeak output and clear shell input buffer !!

    // register message handler
    purple_signal_connect(conv_handle, "received-im-msg",
            plugin, PURPLE_CALLBACK(message_receive), NULL);
    purple_signal_connect(conv_handle, "received-chat-msg",
            plugin, PURPLE_CALLBACK(message_receive), NULL);

    // print some debug info
    purple_debug_info(PLUGIN_NAME, "loaded\n");

    return TRUE;
}

static gboolean plugin_unload(PurplePlugin * plugin)
{
    void *conv_handle = purple_conversations_get_handle();

    // unregister command handlers
    purple_cmd_unregister(tts_command_id_global);
    purple_cmd_unregister(tts_command_id_conversation);
    purple_cmd_unregister(tts_command_id_keyword);
    purple_cmd_unregister(tts_command_id_replace);

    // unregister message handler
    purple_signal_disconnect(conv_handle, "received-im-msg", plugin, PURPLE_CALLBACK(message_receive));
    purple_signal_disconnect(conv_handle, "received-chat-msg", plugin, PURPLE_CALLBACK(message_receive));

    // close connection to child
    close(tts_queue_stdin);
    // TODO: wait for child?
    tts_queue_stdin = 0;
    tts_queue_pid =0;

    // print some debug info:
    purple_debug_info(PLUGIN_NAME, "unloaded\n");

    me = NULL;
    return TRUE;
}
// 1}}}

