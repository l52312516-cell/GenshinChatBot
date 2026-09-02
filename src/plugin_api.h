#ifndef CHATGIBOT_PLUGIN_API_H
#define CHATGIBOT_PLUGIN_API_H

#include <windows.h>

#define CHATGIBOT_PLUGIN_API_VERSION 1

typedef void (*ChatGIBotPluginLogFn)(void *context, const wchar_t *level, const wchar_t *message);
typedef BOOL (*ChatGIBotPluginSendFn)(void *context, const wchar_t *message);
typedef BOOL (*ChatGIBotPluginRegisterCommandFn)(void *context, const wchar_t *command,
                                                 BOOL (*handler)(void *handler_context, const wchar_t *args),
                                                 void *handler_context);
typedef BOOL (*ChatGIBotPluginGetConfigFn)(void *context, const wchar_t *key,
                                            wchar_t *value, int value_chars);

typedef struct {
    int api_version;
    void *context;
    ChatGIBotPluginLogFn log;
    ChatGIBotPluginSendFn send_message;
    ChatGIBotPluginRegisterCommandFn register_command;
    ChatGIBotPluginGetConfigFn get_config;
} ChatGIBotPluginHost;

typedef struct {
    int api_version;
    const wchar_t *name;
    const wchar_t *version;
} ChatGIBotPluginInfo;

typedef BOOL (*ChatGIBotPluginInitFn)(const ChatGIBotPluginHost *host, ChatGIBotPluginInfo *info);
typedef void (*ChatGIBotPluginShutdownFn)(void);

#endif
