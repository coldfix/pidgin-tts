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
// compiler flags {{{2
# ifndef G_GNUC_NULL_TERMINATED
#  if __GNUC__ >= 4
#    define G_GNUC_NULL_TERMINATED __attribute__((__sentinel__))
#  else
#    define G_GNUC_NULL_TERMINATED
#  endif /* __GNUC__ >= 4 */
# endif /* G_GNUC_NULL_TERMINATED */
# define PURPLE_PLUGINS

// purple includes {{{2
# include <pidgin/gtkplugin.h>       // gtk stuff
# include <libpurple/cmds.h>         // purple_cmd_xxx
# include <libpurple/conversation.h> // purple_conversation_xxx
# include <libpurple/debug.h>        // purple_debug_xxx
# include <libpurple/prefs.h>        // purple_pref_xxx
# include <libpurple/signals.h>      // purple_signal_xxx, ...
# include <libpurple/util.h>         // purple_str_xxx
# include <libpurple/version.h>

// system includes {{{2
# include <stdio.h>         // dprintf
# include <stdarg.h>        // va_list
# include <string.h>
# include <unistd.h>        // write, close
# include <errno.h>
# include <sys/types.h>

// plugin info {{{2
# define PLUGIN_ID      "qjuh-pidgin-tts"
# define PLUGIN_NAME    "Pidgin-eSpeak"

// purple config pathes {{{2
# define PREFS_BASE     "/plugins/core/pidgin-tts"
# define PREFS_ACTIVE   PREFS_BASE "/active"
# define PREFS_SHELL    PREFS_BASE "/shell"

# define PREFS_BUDDY    PREFS_BASE "/buddy/%s"

# define PREFS_PROFILE  PREFS_BASE    "/profile"
# define PREFS_PROFILES PREFS_BASE    "/profile/%s"
# define PREFS_COMMAND  PREFS_PROFILES  "/command"
# define PREFS_COMPOSE  PREFS_PROFILES  "/compose"
# define PREFS_LANGUAGE PREFS_PROFILES  "/language"
# define PREFS_VOLUME   PREFS_PROFILES  "/volume"
# define PREFS_REPLACE  PREFS_PROFILES  "/replace"
# define PREFS_KEYWORDS PREFS_PROFILES  "/keywords"
# define PREFS_KEYS_ON  PREFS_PROFILES  "/keywords-active"

// default settings {{{2
# define DEFAULT_ACTIVE         TRUE
# define DEFAULT_SHELL          "/bin/sh"
# define DEFAULT_PROFILE        PROFILE_ESPEAK

// profiles
# define PROFILE_ESPEAK             "espeak"
# define PROFILE_ESPEAK_COMMAND     "/usr/bin/espeak"
# define PROFILE_ESPEAK_COMPOSE     "%s -v %s -a %s '%s'"
# define PROFILE_ESPEAK_LANGUAGE    "de"
# define PROFILE_ESPEAK_VOLUME      "200"
# define PROFILE_ESPEAK_REPLACE     NULL
# define PROFILE_ESPEAK_KEYWORDS    NULL
# define PROFILE_ESPEAK_KEYS_ON     FALSE

// commands {{{2
# define CMD_TTS                "tts"

# define CMD_ENABLE             "on"
# define CMD_DISABLE            "off"

# define CMD_SHELL              "shell"
# define CMD_BIN                "command"
# define CMD_COMPOSE            "compose"
# define CMD_LANGUAGE           "lang"
# define CMD_REPLACE            "replace"
# define CMD_VOLUME             "volume"
# define CMD_EXTRA              "param"
# define CMD_STATUS             "status"
# define CMD_PROFILE            "profile"
# define CMD_TEST               "test"
# define CMD_SAY                "say"

# define CMD_KEYWORD            "keyword"
# define CMD_KEYWORD_ENABLE     CMD_ENABLE
# define CMD_KEYWORD_DISABLE    CMD_DISABLE
# define CMD_KEYWORD_LIST       "list"
# define CMD_KEYWORD_ADD        "add"
# define CMD_KEYWORD_REMOVE     "remove"

# define CMD_CONV               "buddy"
# define CMD_CONV_ENABLE        CMD_ENABLE
# define CMD_CONV_DISABLE       CMD_DISABLE

// forward declarations {{{2
static void ptts_plugin_init(PurplePlugin *plugin);
static gboolean ptts_plugin_load(PurplePlugin *plugin);
static gboolean ptts_plugin_unload(PurplePlugin * plugin);

// instance variables {{{2
static PurplePlugin *ptts_instance;

// file descriptors
static int
    ptts_queue_stdin,
    ptts_queue_pid;

// command ids
static int
    ptts_command_id_global,
    ptts_command_id_conversation,
    ptts_command_id_keyword,
    ptts_command_id_replace;

static GList
    *active_conversations,
    *inactive_conversations;

// Export plugin {{{1
static PurplePluginInfo pluginInfo =
{
    PURPLE_PLUGIN_MAGIC,
    PURPLE_MAJOR_VERSION,
    PURPLE_MINOR_VERSION,
    PURPLE_PLUGIN_STANDARD,                             // type
    NULL,                                               // ui_requirement
    0,                                                  // flags
    NULL,                                               // dependencies
    PURPLE_PRIORITY_DEFAULT,                            // priority

    PLUGIN_ID,                                          // id
    PLUGIN_NAME,                                        // name
    "1.1",                                              // version
    "Read incoming text messages.",                     // summary
    "Reads incoming text messages via espeak.",         // detailed description
    "Thomas Gläßle <t_glaessle@gmx.de>",                // author
    "https://github.com/thomas-glaessle/pidgin-tts",    // homepage
    ptts_plugin_load,                                   // loader
    ptts_plugin_unload,                                 // unloader
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

PURPLE_INIT_PLUGIN(Pidgin_eSpeak, ptts_plugin_init, pluginInfo);


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

// Preferences {{{1
// helpers {{{2
# define TYPE_bool()          gboolean
# define TYPE_string_list()   GList*
# define TYPE_string()        const char*

# define PP_PRINTF_W(TYPE, ACTION) \
    static void pp_##ACTION##_##TYPE(TYPE_##TYPE() value, const gchar* format, ...) __attribute__((format(printf,2,3))); \
    static void pp_##ACTION##_##TYPE(TYPE_##TYPE() value, const gchar* format, ...) \
    { \
        va_list ap; \
        gchar *name; \
        va_start(ap, format); \
        name = g_strdup_vprintf(format, ap); \
        va_end(ap); \
        purple_prefs_##ACTION##_##TYPE(name, value); \
        g_free(name); \
    } \

# define PP_PRINTF(TYPE) \
    static TYPE_##TYPE() pp_get_##TYPE(const gchar* format, ...) __attribute__((format(printf,1,2))); \
    static TYPE_##TYPE() pp_get_##TYPE(const gchar* format, ...) \
    { \
        va_list ap; \
        gchar *name; \
        TYPE_##TYPE() value; \
        va_start(ap, format); \
        name = g_strdup_vprintf(format, ap); \
        va_end(ap); \
        value = purple_prefs_get_##TYPE(name); \
        g_free(name); \
        return value; \
    } \
    PP_PRINTF_W(TYPE, set) \
    PP_PRINTF_W(TYPE, add)

# define PP_PROFILE(TYPE) \
    static TYPE_##TYPE() ppp_get_##TYPE(const gchar* name) \
    { \
        return pp_get_##TYPE(name, pref_get_profile()); \
    } \
    static void ppp_set_##TYPE(const gchar* name, TYPE_##TYPE() value) \
    { \
        pp_set_##TYPE(value, name, pref_get_profile()); \
    } \
    static void ppp_add_##TYPE(const gchar* name, TYPE_##TYPE() value) \
    { \
        pp_add_##TYPE(value, name, pref_get_profile()); \
    }

# define PP_ITEM(PREFIX, NAME, PATH, TYPE) \
    static TYPE_##TYPE() pref_get_##NAME() \
    { \
        return PREFIX##_get_##TYPE(PATH); \
    } \
    static void pref_set_##NAME(TYPE_##TYPE() value) \
    { \
        PREFIX##_set_##TYPE(PATH, value); \
    } \
    static void pref_add_##NAME(TYPE_##TYPE() value) \
    { \
        PREFIX##_add_##TYPE(PATH, value); \
    }

# define PP_PP(TYPE) \
    PP_PRINTF(TYPE) \
    PP_PROFILE(TYPE)

// purple prefs {{{2
PP_ITEM(purple_prefs, profile, PREFS_PROFILE, string);

PP_PP(string);
PP_PP(bool);
PP_PP(string_list);

PP_ITEM(purple_prefs, active,   PREFS_ACTIVE,   bool);
PP_ITEM(purple_prefs, shell,    PREFS_SHELL,    string);

PP_ITEM(ppp, command,           PREFS_COMMAND,  string);
PP_ITEM(ppp, compose,           PREFS_COMPOSE,  string);
PP_ITEM(ppp, language,          PREFS_LANGUAGE, string);
PP_ITEM(ppp, volume,            PREFS_VOLUME,   string);

PP_ITEM(ppp, keywords_active,   PREFS_KEYS_ON,  bool);
PP_ITEM(ppp, keywords,          PREFS_KEYWORDS, string_list);

PP_ITEM(ppp, replacement,       PREFS_REPLACE,  string_list);

/* logging {{{2 */
static void pref_log_active(PurpleConversation *conv)
{
    systemlog(conv,
            "%s is %s",
            PLUGIN_NAME,
            pref_get_active() ? "enabled" : "disabled");
}

static void pref_log_shell(PurpleConversation *conv)
{
    systemlog(conv,
            "%s shell is: %s",
            PLUGIN_NAME,
            pref_get_shell());
}

static void pref_log_profile(PurpleConversation *conv)
{
    systemlog(conv,
            "%s profile is: %s",
            PLUGIN_NAME,
            pref_get_profile());
}

static void pref_log_command(PurpleConversation *conv)
{
    systemlog(conv,
            "%s command is: %s",
            PLUGIN_NAME,
            pref_get_command());
}

static void pref_log_compose(PurpleConversation *conv)
{
    systemlog(conv,
            "%s parameters are: %s",
            PLUGIN_NAME,
            pref_get_compose());
}

static void pref_log_language(PurpleConversation *conv)
{
    systemlog(conv,
            "%s language is: %s",
            PLUGIN_NAME,
            pref_get_language());
}

static void pref_log_volume(PurpleConversation *conv)
{
    systemlog(conv,
            "%s volume is: %s",
            PLUGIN_NAME,
            pref_get_volume());
}

static void pref_log_keywords_active(PurpleConversation *conv)
{
    systemlog(conv,
            "%s keywords are: %s",
            PLUGIN_NAME,
            pref_get_keywords_active() ? "enabled" : "disabled");
}


// Keyword management {{{1
static void pref_delete_keyword(const gchar* keyword)
{
    GList *table = pref_get_keywords(),
          *match = list_find(table, keyword, 1);
    if (match != NULL) {
        g_free(g_list_nth_data(match, 0));
        table = g_list_delete_link(table, match);
        pref_set_keywords(table);
    }
}

static void pref_add_keyword(const gchar* keyword)
{
    GList *table = pref_get_keywords(),
          *match = list_find(table, keyword, 1);
    if (match == NULL) {
        table = g_list_prepend(table, g_strdup(keyword));
        pref_set_keywords(table);
    }
}

static void pref_log_keywords(PurpleConversation *conv)
{
    gchar *tmp, *str;
    GList *table = pref_get_keywords();
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
    GList *table = pref_get_replacement(),
          *match = list_find(table, pattern, 2);
    if (match != NULL) {
        g_free(g_list_nth_data(match, 1));
        g_free(g_list_nth_data(match, 0));
        table = g_list_delete_link(table, g_list_nth(match, 1));
        table = g_list_delete_link(table, g_list_nth(match, 0));
        pref_set_replacement(table);
    }
}

static void pref_add_replace(const gchar* pattern, const gchar* replace)
{
    GList *table;
    pref_delete_replace(pattern);
    table = pref_get_replacement();
    table = g_list_prepend(table, g_strdup(replace));
    table = g_list_prepend(table, g_strdup(pattern));
    pref_set_replacement(table);
}

static void pref_log_replace(PurpleConversation *conv)
{
    gchar *tmp, *str = g_strdup(PLUGIN_NAME " active replacements:");
    GList *table = pref_get_replacement();

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
        systemlog(conv,
                "%s is %s for this conversation",
                PLUGIN_NAME,
                conv_get_active(conv) ? "enabled" : "disabled");
    else
        systemlog(conv,
                "%s uses the default setting (%s) for this conversation",
                PLUGIN_NAME,
                pref_get_active() ? "enabled" : "disabled");
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
    table = pref_get_replacement();

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

    int written = dprintf(ptts_queue_stdin,
        pref_get_compose(),
        pref_get_command(),
        pref_get_language(),
        pref_get_volume(),
        message);

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
            keywords = pref_get_keywords();
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
static PurpleCmdRet ptts_command_keyword(
        PurpleConversation *conv,
        const gchar *cmd,
        gchar **args,
        gchar **error,
        void *data)
{
    if (args[0] == NULL || !purple_strequal(args[0], CMD_KEYWORD))
        return PURPLE_CMD_RET_CONTINUE;

    else if (args[1] == NULL)   // list all keywords
        pref_log_keywords_active(conv);

    else if (args[2] == NULL)
    {
        if (purple_strequal(args[1], CMD_KEYWORD_ENABLE)) {
            pref_set_keywords_active(TRUE);
            pref_log_keywords_active(conv);
        }
        else if (purple_strequal(args[1], CMD_KEYWORD_DISABLE)) {
            pref_set_keywords_active(FALSE);
            pref_log_keywords_active(conv);
        }

        else if (purple_strequal(args[1], CMD_KEYWORD_LIST))
            pref_log_keywords(conv);

        else
            return PURPLE_CMD_RET_FAILED;
    }
    else
    {
        if (purple_strequal(args[1], CMD_KEYWORD_ADD))
            pref_add_keyword(args[2]);

        else if (purple_strequal(args[1], CMD_KEYWORD_REMOVE))
            pref_delete_keyword(args[2]);

        else
            return PURPLE_CMD_RET_FAILED;
    }

    return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet ptts_command_replace(
        PurpleConversation *conv,
        const gchar *cmd,
        gchar **args,
        gchar **error,
        void *data)
{
    if (args[0] == NULL || !purple_strequal(args[0], CMD_REPLACE))
        return PURPLE_CMD_RET_CONTINUE;

    if (args[1] == NULL)
        pref_log_replace(conv);
    else if (args[2] == NULL) {
        pref_delete_replace(args[1]);
        systemlog(conv,
                "%s - deleted replacement for: %s",
                PLUGIN_NAME,
                args[1]);
    }
    else {
        pref_add_replace(args[1], args[2]);
        systemlog(conv,
                "%s - added replacement for: %s",
                PLUGIN_NAME,
                args[1]);
    }

    return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet ptts_command_conv(
        PurpleConversation *conv,
        const gchar *cmd,
        gchar **args,
        gchar **error,
        void *data)
{
    if (args[0] == NULL || !purple_strequal(args[0], CMD_CONV))
        return PURPLE_CMD_RET_CONTINUE;

    if (args[1] == NULL)
        conv_log_active(conv);

    else if (purple_strequal(args[1], CMD_CONV_ENABLE)) {
        if (conv_get_inactive(conv) && pref_get_active())
            conv_set_inactive(conv, FALSE);
        else
            conv_set_active(conv, TRUE);
        conv_log_active(conv);
    }

    else if (purple_strequal(args[1], CMD_CONV_DISABLE)) {
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

static PurpleCmdRet ptts_command(
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
            if (purple_strequal(args[0], CMD_ENABLE)) {
                pref_set_active(TRUE);
                conv_set_inactive(conv, FALSE);
                pref_log_active(conv);
            }

            else if (purple_strequal(args[0], CMD_DISABLE)) {
                pref_set_active(FALSE);
                conv_set_active(conv, FALSE);
                pref_log_active(conv);
            }

            else if (purple_strequal(args[0], CMD_SHELL))
                pref_log_shell(conv);

            else if (purple_strequal(args[0], CMD_BIN))
                pref_log_command(conv);

            else if (purple_strequal(args[0], CMD_COMPOSE))
                pref_log_compose(conv);

            else if (purple_strequal(args[0], CMD_STATUS)) {
                pref_log_active(conv);
                conv_log_active(conv);
                pref_log_shell(conv);
                pref_log_command(conv);
                pref_log_compose(conv);
                pref_log_keywords_active(conv);
                pref_log_keywords(conv);
                pref_log_replace(conv);
            }

            else
                return PURPLE_CMD_RET_FAILED;
        }
        else
        {
            if (purple_strequal(args[0], CMD_SHELL)) {
                pref_set_shell(args[1]);
                pref_log_shell(conv);
            }

            else if (purple_strequal(args[0], CMD_PROFILE)) {
                pref_set_profile(args[1]);
                pref_log_profile(conv);
            }

            else if (purple_strequal(args[0], CMD_BIN)) {
                pref_set_command(args[1]);
                pref_log_command(conv);
            }

            else if (purple_strequal(args[0], CMD_COMPOSE)) {
                pref_set_compose(args[1]);
                pref_log_compose(conv);
            }

            else if (purple_strequal(args[0], CMD_LANGUAGE)) {
                pref_set_language(args[1]);
                pref_log_language(conv);
            }

            else if (purple_strequal(args[0], CMD_VOLUME)) {
                pref_set_volume(args[1]);
                pref_log_volume(conv);
            }

            else if (purple_strequal(args[0], CMD_SAY)) {
                gchar* text;
                if (analyse(args[1], &text)) {
                    tts(conv, text);
                    g_free(text);
                }
            }

            else if (purple_strequal(args[0], CMD_TEST)) {
                if (process_message(conv, args[1]))
                    systemlog(conv,
                            "%s - echoing test string...",
                            PLUGIN_NAME);
                else
                    systemlog(conv,
                            "%s - not echoing test string",
                            PLUGIN_NAME);
            }

            else
                return PURPLE_CMD_RET_FAILED;
        }
    }
    return PURPLE_CMD_RET_OK;
}

// Initialization {{{1
static void ptts_plugin_init(PurplePlugin *plugin)
{
    purple_prefs_add_none(PREFS_BASE);

    pref_add_active(DEFAULT_ACTIVE);
    pref_add_shell(DEFAULT_SHELL);
    pref_add_profile(DEFAULT_PROFILE);

    pref_add_command(PROFILE_ESPEAK_COMMAND);
    pref_add_compose(PROFILE_ESPEAK_COMPOSE);
    pref_add_language(PROFILE_ESPEAK_LANGUAGE);
    pref_add_volume(PROFILE_ESPEAK_VOLUME);

    pref_add_replacement(PROFILE_ESPEAK_REPLACE);
    pref_add_keywords(PROFILE_ESPEAK_KEYWORDS);
    pref_add_keywords_active(PROFILE_ESPEAK_KEYS_ON);
}

static gboolean ptts_plugin_load(PurplePlugin *plugin)
{
    void *conv_handle = purple_conversations_get_handle();
    gchar
        *info_keyword = "/"CMD_TTS" keyword [add &lt;keyword&gt; | remove &lt;keyword&gt;]",
        *info_replace = "/"CMD_TTS" replace &lt;word&gt; &lt;replacement&gt;",
        *info = "/"CMD_TTS" [on | off | compose &lt;command line composition&gt; | shell &lt;path&gt; | command &lt;path&gt; | say &lt;text&gt; | status]",
        *info_stts = "/"CMD_TTS" buddy [on | off]";

    PurpleCmdFlag flags =
        PURPLE_CMD_FLAG_IM | PURPLE_CMD_FLAG_CHAT
        | PURPLE_CMD_FLAG_ALLOW_WRONG_ARGS;


    ptts_instance = plugin;

    // start child process
    ptts_queue_pid = spawn(pref_get_shell(), NULL, 0, &ptts_queue_stdin, NULL);

    // register command handlers
    ptts_command_id_global = purple_cmd_register(
            CMD_TTS,                            // command name
            "ws",                               // command argument format
            PURPLE_CMD_P_DEFAULT,               // command priority flags
            flags,                              // command usage flags
            PLUGIN_ID,                          // Plugin ID
            ptts_command,                       // Name of the callback function
            info,                               // Help message
            NULL );                             // Any special user-defined data
    ptts_command_id_conversation = purple_cmd_register(
            CMD_TTS,                            // command name: stts = single tts
            "wws",                              // command argument format
            PURPLE_CMD_P_DEFAULT,               // command priority flags
            flags,                              // command usage flags
            PLUGIN_ID,                          // Plugin ID
            ptts_command_conv,                  // Name of the callback function
            info_stts,                          // Help message
            NULL );                             // Any special user-defined data
    ptts_command_id_keyword = purple_cmd_register(
            CMD_TTS,                            // command name
            "wws",                              // command argument format
            PURPLE_CMD_P_DEFAULT,               // command priority flags
            flags,                              // command usage flags
            PLUGIN_ID,                          // Plugin ID
            ptts_command_keyword,               // Name of the callback function
            info_keyword,                       // Help message
            NULL );                             // Any special user-defined data
    ptts_command_id_replace = purple_cmd_register(
            CMD_TTS,                            // command name
            "wws",                              // command argument format
            PURPLE_CMD_P_DEFAULT,               // command priority flags
            flags,                              // command usage flags
            PLUGIN_ID,                          // Plugin ID
            ptts_command_replace,               // Name of the callback function
            info_replace,                       // Help message
            NULL );                             // Any special user-defined data


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

static gboolean ptts_plugin_unload(PurplePlugin * plugin)
{
    void *conv_handle = purple_conversations_get_handle();

    // unregister command handlers
    purple_cmd_unregister(ptts_command_id_global);
    purple_cmd_unregister(ptts_command_id_conversation);
    purple_cmd_unregister(ptts_command_id_keyword);
    purple_cmd_unregister(ptts_command_id_replace);

    // unregister message handler
    purple_signal_disconnect(conv_handle, "received-im-msg", plugin, PURPLE_CALLBACK(message_receive));
    purple_signal_disconnect(conv_handle, "received-chat-msg", plugin, PURPLE_CALLBACK(message_receive));

    // close connection to child
    close(ptts_queue_stdin);
    // TODO: wait for child?
    ptts_queue_stdin = 0;
    ptts_queue_pid =0;

    // print some debug info:
    purple_debug_info(PLUGIN_NAME, "unloaded\n");

    ptts_instance = NULL;
    return TRUE;
}
// 1}}}

