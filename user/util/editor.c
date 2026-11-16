#include "common.h"
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "untitled.txt";
    int descriptor = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (descriptor < 0) { printf("editor: cannot open %s\n", path); return 1; }
    puts("Enter text. Ctrl-D saves. Backspace edits the current line.");
    char text[4096];
    size_t length = 0;
    for (;;) {
        int character = getchar();
        if (character < 0 || character == 4) break;
        if (character == '\b') {
            if (length && text[length - 1] != '\n') { --length; write(STDOUT_FILENO, "\b \b", 3); }
            continue;
        }
        if (length == sizeof(text)) break;
        text[length++] = (char)character;
        putchar(character);
    }
    size_t sent = 0;
    while (sent < length) {
        ssize_t count = write(descriptor, text + sent, length - sent);
        if (count <= 0) { close(descriptor); puts("editor: write failed"); return 1; }
        sent += (size_t)count;
    }
    close(descriptor);
    return 0;
}
