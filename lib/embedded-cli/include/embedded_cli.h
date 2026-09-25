#ifndef EMBEDDED_CLI_H
#define EMBEDDED_CLI_H


#ifdef __cplusplus

extern "C" {
#else

#include <stdbool.h>

#endif

// cstdint is available only since C++11, so use C header
#include <stdint.h>
// [PATCH: Single-write redraw] size_t for embeddedCliPrintToBuffer below.
// stdint.h is not required to declare it; stddef.h is the header that does.
#include <stddef.h>

// used for proper alignment of cli buffer
#if UINTPTR_MAX == 0xFFFF
#define CLI_UINT uint16_t
#elif UINTPTR_MAX == 0xFFFFFFFF
#define CLI_UINT uint32_t
#elif UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
#define CLI_UINT uint64_t
#else
#error unsupported pointer size
#endif

#define CLI_UINT_SIZE (sizeof(CLI_UINT))
// convert size in bytes to size in terms of CLI_UINTs (rounded up
// if bytes is not divisible by size of single CLI_UINT)
#define BYTES_TO_CLI_UINTS(bytes) \
  (((bytes) + CLI_UINT_SIZE - 1)/CLI_UINT_SIZE)

typedef struct CliCommand CliCommand;
typedef struct CliCommandBinding CliCommandBinding;
typedef struct EmbeddedCli EmbeddedCli;
typedef struct EmbeddedCliConfig EmbeddedCliConfig;


struct CliCommand {
    /**
     * Name of the command.
     * In command "set led 1 1" "set" is name
     */
    const char *name;

    /**
     * String of arguments of the command.
     * In command "set led 1 1" "led 1 1" is string of arguments
     * Is ended with double 0x00 char
     * Use tokenize functions to easily get individual tokens
     */
    char *args;
};

/**
 * Struct to describe binding of command to function and
 */
struct CliCommandBinding {
    /**
     * Name of command to bind. Should not be NULL.
     */
    const char *name;

    /**
     * Help string that will be displayed when "help <cmd>" is executed.
     * Can have multiple lines separated with "\r\n"
     * Can be NULL if no help is provided.
     */
    const char *help;

    /**
     * Flag to perform tokenization before calling binding function.
     */
    bool tokenizeArgs;

    /**
     * Pointer to any specific app context that is required for this binding.
     * It will be provided in binding callback.
     */
    void *context;

    /**
     * Binding function for when command is received.
     * If null, default callback (onCommand) will be called.
     * @param cli - pointer to cli that is calling this binding
     * @param args - string of args (if tokenizeArgs is false) or tokens otherwise
     * @param context
     */
    void (*binding)(EmbeddedCli *cli, char *args, void *context);
};

struct EmbeddedCli {
    /**
     * Should write char to connection
     * @param cli - pointer to cli that executed this function
     * @param c   - actual character to write
     */
    void (*writeChar)(EmbeddedCli *cli, char c);

    /**
     * Called when command is received and command not found in list of
     * command bindings (or binding function is null).
     * @param cli     - pointer to cli that executed this function
     * @param command - pointer to received command
     */
    void (*onCommand)(EmbeddedCli *cli, CliCommand *command);

    /**
     * [PATCH: Catalog completion callback] Optional external source of Tab
     * completion candidates. When set, it REPLACES bindings-based completion
     * entirely for this cli instance (bindings and this source are never
     * merged) and completes the CURRENT TOKEN of the command buffer - the
     * substring after the last space, or the whole buffer if there is none -
     * rather than the whole line. This is what lets one callback complete
     * both an operation name (first token) and a later argument key (a
     * later token) from the same mechanism, and it avoids the RAM cost of
     * one CliCommandBinding (20 B) per candidate: a large candidate set (a
     * project catalog) can be enumerated in place instead of copied into
     * cli->bindings via embeddedCliAddBinding().
     *
     * Called with index = 0, 1, 2, ... until it returns NULL, which ends
     * enumeration for that call. A completion scan calls this repeatedly and
     * keeps referencing the FIRST matching candidate's pointer for the rest
     * of the scan (to compute the shared prefix across candidates), so a
     * returned pointer must stay valid and byte-identical for the remainder
     * of one scan: a single shared scratch buffer overwritten on every call
     * is NOT safe here. A stable pointer (e.g. into a project's flash-
     * resident table) or an index-stable pool of scratch buffers both are.
     * @param cli - pointer to cli that is calling this function
     * @param index - zero-based candidate index for the current token
     * @return candidate token text, or NULL when index is out of range
     */
    const char *(*getCompletionCandidate)(EmbeddedCli *cli, uint16_t index);

    /**
     * [PATCH: History filter callback] Optional predicate consulted before a
     * submitted command line is stored in the Up/Down history ring. When it
     * is NULL - the default, and every other consumer's state - every
     * non-empty line is stored exactly as upstream stores it, so this patch
     * costs nothing that is not asked for.
     *
     * Called from parseCommand() with the line as typed, BEFORE the command
     * buffer is split into name and args, and BEFORE historyPut(). Deciding
     * first is the point of the callback rather than removing the line
     * afterwards: a value that reaches the ring has been in a buffer that
     * outlives the decision, and Up-arrow is not that buffer's only reader.
     * Returning false suppresses the history write only - the line still
     * dispatches normally, because refusing to REMEMBER a command is not
     * refusing to run it (the project's own use is a line the dispatcher
     * must reach in order to answer that it is refused).
     *
     * The line is owned by the cli and is only valid for the duration of the
     * call; a predicate that needs to keep anything must copy it, and one
     * looking at a secret should copy nothing.
     * @param cli  - pointer to cli that is calling this function
     * @param line - the submitted command line, NUL-terminated
     * @return true to store the line in history, false to leave it out
     */
    bool (*shouldStoreHistory)(EmbeddedCli *cli, const char *line);

    /**
     * [PATCH: Explicit line-too-long] Optional notification that the line the
     * operator just submitted lost at least one byte before it could be
     * stored - a displayable character past the fixed command buffer, or a
     * byte the rx FIFO could not accept - and has therefore been discarded
     * whole instead of dispatched.
     *
     * The DISCARD is unconditional and happens with or without this callback:
     * upstream ignored the excess bytes and then ran whatever fit, so a
     * command the operator never typed could execute (an argument list
     * clipped mid-value, a configuration write missing its last field). Not
     * running a truncated command is a safety property of the patch. This
     * callback is only how the caller gets to SAY so - leave it NULL and the
     * line is still refused, silently.
     *
     * Called from onControlInput's CR/LF branch, in place of parseCommand():
     * onCommand does not fire for that line, nothing is written to the
     * history ring, and no autocompletion runs on it. The command buffer is
     * cleared and the invitation reprinted immediately afterwards, exactly as
     * for an ordinary submitted line, so the next line starts clean.
     *
     * One call per submitted line, however many bytes were lost, and CRLF
     * still counts as one line ending.
     * @param cli - pointer to cli that is calling this function
     */
    void (*onLineTooLong)(EmbeddedCli *cli);

    /**
     * Can be used for any application context
     */
    void *appContext;

    /**
     * Pointer to actual implementation, do not use.
     */
    void *_impl;
};

/**
 * Configuration to create CLI
 */
struct EmbeddedCliConfig {
    /**
     * Invitation string. Is printed at the beginning of each line with user
     * input
     */
    const char *invitation;
    
    /**
     * Size of buffer that is used to store characters until they're processed
     */
    uint16_t rxBufferSize;

    /**
     * Size of buffer that is used to store current input that is not yet
     * sended as command (return not pressed yet)
     */
    uint16_t cmdBufferSize;

    /**
     * Size of buffer that is used to store previously entered commands
     * Only unique commands are stored in buffer. If buffer is smaller than
     * entered command (including arguments), command is discarded from history
     */
    uint16_t historyBufferSize;

    /**
     * Maximum amount of bindings that can be added via addBinding function.
     * Cli increases takes extra bindings for internal commands:
     * - help
     */
    uint16_t maxBindingCount;

    /**
     * Buffer to use for cli and all internal structures. If NULL, memory will
     * be allocated dynamically. Otherwise this buffer is used and no
     * allocations are made
     */
    CLI_UINT *cliBuffer;

    /**
     * Size of buffer for cli and internal structures (in bytes).
     */
    uint16_t cliBufferSize;

    /**
     * Whether autocompletion should be enabled.
     * If false, autocompletion is disabled but you still can use 'tab' to
     * complete current command manually.
     */
    bool enableAutoComplete;
};

/*
 * [PATCH: Compile-time required size] The layout embeddedCliNew() carves out of
 * cliBuffer, moved here from embedded_cli.c so that a caller can size a static
 * buffer at compile time with EMBEDDED_CLI_REQUIRED_SIZE() below instead of
 * finding out at run time that it is too small. The library's own code is
 * unchanged; only where these definitions live is.
 */
typedef struct EmbeddedCliImpl EmbeddedCliImpl;
typedef struct FifoBuf FifoBuf;
typedef struct CliHistory CliHistory;

struct FifoBuf {
    char *buf;
    /**
     * Position of first element in buffer. From this position elements are taken
     */
    uint16_t front;
    /**
     * Position after last element. At this position new elements are inserted
     */
    uint16_t back;
    /**
     * Size of buffer
     */
    uint16_t size;
};

struct CliHistory {
    /**
     * Items in buffer are separated by null-chars
     */
    char *buf;

    /**
     * Total size of buffer
     */
    uint16_t bufferSize;

    /**
     * Index of currently selected element. This allows to navigate history
     * After command is sent, current element is reset to 0 (no element)
     */
    uint16_t current;

    /**
     * Number of items in buffer
     * Items are counted from top to bottom (and are 1 based).
     * So the most recent item is 1 and the oldest is itemCount.
     */
    uint16_t itemsCount;
};

struct EmbeddedCliImpl {
    /**
     * Invitation string. Is printed at the beginning of each line with user
     * input
     */
    const char *invitation;

    CliHistory history;

    /**
     * Buffer for storing received chars.
     * Chars are stored in FIFO mode.
     */
    FifoBuf rxBuffer;

    /**
     * Buffer for current command
     */
    char *cmdBuffer;

    /**
     * Size of current command
     */
    uint16_t cmdSize;

    /**
     * Total size of command buffer
     */
    uint16_t cmdMaxSize;

    CliCommandBinding *bindings;

    /**
     * Flags for each binding. Sizes are the same as for bindings array
     */
    uint8_t *bindingsFlags;

    uint16_t bindingsCount;

    uint16_t maxBindingsCount;

    /**
     * Total length of input line. This doesn't include invitation but
     * includes current command and its live autocompletion
     */
    uint16_t inputLineLength;

    /**
     * Stores last character that was processed.
     */
    char lastChar;

    /**
     * Flags are defined as CLI_FLAG_*
     */
    uint8_t flags;

    /**
     * Cursor position for current command from right to left
     * 0 = end of command
     */
    uint16_t cursorPos;

    /**
     * [PATCH: Single-write redraw] Capture target for embeddedCliPrintToBuffer.
     * While non-NULL, every character this library would hand to
     * cli->writeChar is appended here instead (writeCharOut below), so a
     * caller can render a whole redraw and hand it to its transport in ONE
     * write. NULL - the state outside that one call - restores the upstream
     * behavior exactly: straight through to cli->writeChar, character by
     * character.
     */
    char *outBuffer;

    /**
     * Capacity of outBuffer. Meaningful only while outBuffer is non-NULL.
     */
    size_t outCapacity;

    /**
     * Bytes appended to outBuffer so far.
     */
    size_t outLength;

    /**
     * Set when a character did not fit outBuffer. The partial content is then
     * never handed back: a half-rendered redraw on the wire is worse than no
     * redraw at all, so embeddedCliPrintToBuffer reports the whole render as
     * not fitting and the caller falls back to whatever it can send whole.
     */
    bool outOverflow;
};

/*
 * [PATCH: Compile-time required size] Commands the cli adds itself (upstream:
 * one, the internal help). Zero here: see "Project-help ownership".
 */
#define EMBEDDED_CLI_INTERNAL_BINDING_COUNT 0

/*
 * [PATCH: Compile-time required size] What embeddedCliRequiredSize() returns
 * for a configuration with these four sizes, as a constant expression, so a
 * static buffer can be declared exactly that large:
 *
 *     static CLI_UINT buf[BYTES_TO_CLI_UINTS(EMBEDDED_CLI_REQUIRED_SIZE(64, 64, 128, 8))];
 *
 * embeddedCliRequiredSize() is defined as this macro, so the two cannot drift.
 */
#define EMBEDDED_CLI_REQUIRED_SIZE(rxBufferSize, cmdBufferSize, historyBufferSize,            \
                                   maxBindingCount)                                           \
    (CLI_UINT_SIZE *                                                                          \
     (BYTES_TO_CLI_UINTS(sizeof(EmbeddedCli)) + BYTES_TO_CLI_UINTS(sizeof(EmbeddedCliImpl)) + \
      BYTES_TO_CLI_UINTS((rxBufferSize) * sizeof(char)) +                                     \
      BYTES_TO_CLI_UINTS((cmdBufferSize) * sizeof(char)) +                                    \
      BYTES_TO_CLI_UINTS((historyBufferSize) * sizeof(char)) +                                \
      BYTES_TO_CLI_UINTS(((maxBindingCount) + EMBEDDED_CLI_INTERNAL_BINDING_COUNT) *          \
                         sizeof(CliCommandBinding)) +                                         \
      BYTES_TO_CLI_UINTS(((maxBindingCount) + EMBEDDED_CLI_INTERNAL_BINDING_COUNT) *          \
                         sizeof(uint8_t))))

/**
 * Returns pointer to default configuration for cli creation. It is safe to
 * modify it and then send to embeddedCliNew().
 * Returned structure is always the same so do not free and try to use it
 * immediately.
 * Default values:
 * <ul>
 * <li>rxBufferSize = 64</li>
 * <li>cmdBufferSize = 64</li>
 * <li>historyBufferSize = 128</li>
 * <li>cliBuffer = NULL (use dynamic allocation)</li>
 * <li>cliBufferSize = 0</li>
 * <li>maxBindingCount = 8</li>
 * <li>enableAutoComplete = true</li>
 * </ul>
 * @return configuration for cli creation
 */
EmbeddedCliConfig *embeddedCliDefaultConfig(void);

/**
 * Returns how many space in config buffer is required for cli creation
 * If you provide buffer with less space, embeddedCliNew will return NULL
 * This amount will always be divisible by CLI_UINT_SIZE so allocated buffer
 * and internal structures can be properly aligned
 * @param config
 * @return
 */
uint16_t embeddedCliRequiredSize(EmbeddedCliConfig *config);

/**
 * Create new CLI.
 * Memory is allocated dynamically if cliBuffer in config is NULL.
 * After CLI is created, override function pointers to start using it
 * @param config - config for cli creation
 * @return pointer to created CLI
 */
EmbeddedCli *embeddedCliNew(EmbeddedCliConfig *config);

/**
 * Same as calling embeddedCliNew with default config.
 * @return
 */
EmbeddedCli *embeddedCliNewDefault(void);

/**
 * Receive character and put it to internal buffer
 * Actual processing is done inside embeddedCliProcess
 * You can call this function from something like interrupt service routine,
 * just make sure that you call it only from single place. Otherwise input
 * might get corrupted
 * @param cli
 * @param c   - received char
 */
void embeddedCliReceiveChar(EmbeddedCli *cli, char c);

/**
 * Process rx/tx buffers. Command callbacks are called from here
 * @param cli
 */
void embeddedCliProcess(EmbeddedCli *cli);

/**
 * Add specified binding to list of bindings. If list is already full, binding
 * is not added and false is returned
 * @param cli
 * @param binding
 * @return true if binding was added, false otherwise
 */
bool embeddedCliAddBinding(EmbeddedCli *cli, CliCommandBinding binding);

/**
 * Reset the input buffer when an overflow is detected.
 * Clears the partial command without discarding other state.
 * Should be called by the listener when it detects that input has exceeded the buffer size.
 *
 * [PATCH: Explicit line-too-long] Also clears any pending line-too-long
 * refusal (onLineTooLong above): the line it referred to is being abandoned
 * here, so the caller's next Enter must not be answered about it.
 * @param cli
 */
void embeddedCliResetInput(EmbeddedCli *cli);

/**
 * [PATCH: Catalog completion callback] Return the current, not-yet-submitted
 * command buffer (NUL-terminated at its current length). Exposed so an
 * external completion source (EmbeddedCli::getCompletionCandidate) can see
 * what has been typed so far and decide what it is completing - an
 * operation name (no space yet) or an argument key (a complete first token
 * followed by a space).
 * @param cli
 * @return pointer to the internal buffer; its contents change on the next
 * embeddedCliProcess() call, so do not retain the pointer past that.
 */
const char *embeddedCliGetCmdBuffer(const EmbeddedCli *cli);

/**
 * Print specified string and account for currently entered but not submitted
 * command.
 * Current command is deleted, provided string is printed (with new line) after
 * that current command is printed again, so user can continue typing it.
 * @param cli
 * @param string
 */
void embeddedCliPrint(EmbeddedCli *cli, const char *string);

/**
 * [PATCH: Single-write redraw] Render exactly what embeddedCliPrint() would
 * write - clear the input line, the string, a line break, the invitation, the
 * buffered command, the cursor move - into `buffer` instead of handing it to
 * cli->writeChar one character at a time. The line-editor state is updated
 * identically; only the destination differs.
 *
 * A transport whose write can be interleaved by another writer, or torn by
 * backpressure, needs the whole redraw as ONE write. Character-at-a-time gives
 * every other writer on that wire ~70 openings to land a byte inside the line,
 * which is the interleaving docs/console-protocol.md section 6 forbids.
 *
 * Not re-entrant: calling this from inside a render (through cli->writeChar,
 * say) returns 0 rather than interleaving two renders in one buffer.
 *
 * @param cli
 * @param string
 * @param buffer     destination, not NUL-terminated by this function
 * @param bufferSize capacity of buffer
 * @return bytes written to buffer, or 0 if the render did not fit (nothing is
 *         then usable in buffer, and the line-editor state is left as it was,
 *         so the caller may fall back to sending the string on its own)
 */
size_t embeddedCliPrintToBuffer(EmbeddedCli *cli, const char *string,
                                char *buffer, size_t bufferSize);

/**
 * Free allocated for cli memory
 * @param cli
 */
void embeddedCliFree(EmbeddedCli *cli);

/**
 * Perform tokenization of arguments string. Original string is modified and
 * should not be used directly (only inside other token functions).
 * Individual tokens are separated by single 0x00 char, double 0x00 is put at
 * the end of token list. After calling this function, you can use other
 * token functions to get individual tokens and token count.
 *
 * Important: Call this function only once. Otherwise information will be lost if
 * more than one token existed
 * @param args - string to tokenize (must have extra writable char after 0x00)
 * @return
 */
void embeddedCliTokenizeArgs(char *args);

/**
 * Return specific token from tokenized string
 * @param tokenizedStr
 * @param pos (counted from 1)
 * @return token
 */
const char *embeddedCliGetToken(const char *tokenizedStr, uint16_t pos);

/**
 * Same as embeddedCliGetToken but works on non-const buffer
 * @param tokenizedStr
 * @param pos (counted from 1)
 * @return token
 */
char *embeddedCliGetTokenVariable(char *tokenizedStr, uint16_t pos);

/**
 * Find token in provided tokens string and return its position (counted from 1)
 * If no such token is found - 0 is returned.
 * @param tokenizedStr
 * @param token - token to find
 * @return position (increased by 1) or zero if no such token found
 */
uint16_t embeddedCliFindToken(const char *tokenizedStr, const char *token);

/**
 * Return number of tokens in tokenized string
 * @param tokenizedStr
 * @return number of tokens
 */
uint16_t embeddedCliGetTokenCount(const char *tokenizedStr);

#ifdef __cplusplus
}
#endif


#endif //EMBEDDED_CLI_H
