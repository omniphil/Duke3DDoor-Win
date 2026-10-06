/*
 * winlaunch.c -- stands in for Mystic for Windows, to test duke3ddoor.exe without a BBS.
 *
 *     winlaunch.exe <door.exe> [args before the drop folder...]
 *
 * Makes a real telnet-style TCP connection on 127.0.0.1 (nothing listens beyond this machine and only for one
 * connect), writes DOOR32.SYS with comm type 2 and the door's end of that connection as an inheritable socket handle,
 * the way Mystic does, and starts the door with the drop folder as its last argument. The other end of the connection
 * is relayed to this program's stdin/stdout, so a test can play the caller through pipes: 0xFF is doubled towards the
 * door and undoubled from it (telnet), and a few telnet commands are sent first, which the door must swallow.
 * Exits with the door's exit code.
 *
 * Build: x86_64-w64-mingw32-gcc -O2 winlaunch.c -o winlaunch.exe -lws2_32 (tools/test_windoor.py does it).
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

static SOCKET caller = INVALID_SOCKET;

/* stdin -> door, with 0xFF doubled */
static DWORD WINAPI to_door(LPVOID unused)
{
    (void)unused;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    unsigned char buf[8192], out[16384];
    DWORD n;
    while (ReadFile(in, buf, sizeof buf, &n, NULL) && n > 0) {
        size_t m = 0;
        for (DWORD i = 0; i < n; i++) {
            out[m++] = buf[i];
            if (buf[i] == 0xFF)
                out[m++] = 0xFF;
        }
        for (size_t off = 0; off < m;) {
            int s = send(caller, (const char *)out + off, (int)(m - off), 0);
            if (s <= 0)
                return 0;
            off += (size_t)s;
        }
    }
    shutdown(caller, SD_SEND);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: winlaunch <door.exe> [args...]\n");
        return 2;
    }
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return 2;

    /* One connection over loopback: listen, connect to ourselves, accept */
    SOCKET lis = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int alen = sizeof a;
    if (bind(lis, (struct sockaddr *)&a, sizeof a) != 0 || listen(lis, 1) != 0 ||
        getsockname(lis, (struct sockaddr *)&a, &alen) != 0) {
        fprintf(stderr, "winlaunch: loopback listen failed (%d)\n", WSAGetLastError());
        return 2;
    }
    caller = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connect(caller, (struct sockaddr *)&a, sizeof a) != 0) {
        fprintf(stderr, "winlaunch: connect failed (%d)\n", WSAGetLastError());
        return 2;
    }
    SOCKET door = accept(lis, NULL, NULL);
    closesocket(lis);
    if (door == INVALID_SOCKET)
        return 2;
    SetHandleInformation((HANDLE)door, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation((HANDLE)caller, HANDLE_FLAG_INHERIT, 0);

    /* DOOR32.SYS in a fresh folder under %TEMP% */
    char dir[MAX_PATH], path[MAX_PATH];
    GetTempPathA(sizeof dir, dir);
    snprintf(dir + strlen(dir), sizeof dir - strlen(dir), "winlaunch-%lu", (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(dir, NULL);
    snprintf(path, sizeof path, "%s\\door32.sys", dir);
    FILE *f = fopen(path, "w");
    if (!f)
        return 2;
    fprintf(f, "2\n%llu\n38400\nwinlaunch\n1\nTest Player\nplayer\n100\n60\n1\n1\n", (unsigned long long)door);
    fclose(f);

    /* The door, with the drop folder last */
    char cmd[4096] = "";
    for (int i = 1; i < argc; i++)
        snprintf(cmd + strlen(cmd), sizeof cmd - strlen(cmd), "\"%s\" ", argv[i]);
    snprintf(cmd + strlen(cmd), sizeof cmd - strlen(cmd), "\"%s\"", dir);
    STARTUPINFOA si = {sizeof si};
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "winlaunch: could not start the door (%lu)\n", GetLastError());
        return 2;
    }
    closesocket(door);   /* the door has its own copy now */

    /* Telnet negotiation the door must not show or treat as keys: WILL ECHO, WILL SGA, DO NAWS + a NAWS reply */
    static const unsigned char hello[] = {255, 251, 1, 255, 251, 3, 255, 253, 31,
                                          255, 250, 31, 0, 80, 0, 25, 255, 240};
    send(caller, (const char *)hello, sizeof hello, 0);

    CreateThread(NULL, 0, to_door, NULL, 0, NULL);

    /* door -> stdout, with doubled 0xFF undoubled (the door sends no other telnet commands) */
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    unsigned char buf[16384], plain[16384];
    int pending_ff = 0, n;
    while ((n = recv(caller, (char *)buf, sizeof buf, 0)) > 0) {
        DWORD m = 0, w;
        for (int i = 0; i < n; i++) {
            if (pending_ff) {
                pending_ff = 0;
                plain[m++] = 0xFF;
                if (buf[i] == 0xFF)
                    continue;
                fprintf(stderr, "winlaunch: lone 0xFF from the door, then %u\n", buf[i]);
            }
            if (buf[i] == 0xFF)
                pending_ff = 1;
            else
                plain[m++] = buf[i];
        }
        if (m && !WriteFile(out, plain, m, &w, NULL))
            break;
    }

    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    return (int)code;
}
