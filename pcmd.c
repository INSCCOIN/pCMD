/* pCMD — dual-pane commander for SharkDeck. C port of sCMD. */
#define _XOPEN_SOURCE 700
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <ncurses.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define NAME "pCMD"
#define MAXENT 1024
#define MAXMARK 256
#define NAMELEN 256
#define PATHLEN 1024

enum { SORT_NAME, SORT_SIZE, SORT_DATE };

typedef struct {
    char name[NAMELEN];
    char path[PATHLEN];
    int isdir;
    off_t size;
    time_t mtime;
} Ent;

typedef struct {
    char path[PATHLEN];
    Ent ent[MAXENT];
    int n, cur, scroll;
    int hidden, sort;
    char filter[64];
    char marks[MAXMARK][PATHLEN];
    int nmark;
} Pane;

static WINDOW *scr;
static Pane panes[2];
static int active;
static int preview;
static char msg[256];
static int msg_err;

static void put(int y, int x, const char *s, int n, int attr)
{
    int h, w;
    getmaxyx(scr, h, w);
    if (y < 0 || x < 0 || y >= h || x >= w || n <= 0)
        return;
    if (y == h - 1 && x + n >= w)
        n = w - x - 1;
    if (n <= 0)
        return;
    attron(attr);
    mvaddnstr(y, x, s, n);
    attroff(attr);
}

static void say(const char *s, int err)
{
    snprintf(msg, sizeof msg, "%s", s ? s : "");
    msg_err = err;
}

static void abspath_into(char *out, size_t n, const char *p)
{
    char tmp[PATHLEN];
    if (!p || !*p)
        p = ".";
    if (realpath(p, tmp))
        snprintf(out, n, "%s", tmp);
    else
        snprintf(out, n, "%s", p);
}

static int marked(Pane *p, const char *path)
{
    int i;
    for (i = 0; i < p->nmark; i++)
        if (!strcmp(p->marks[i], path))
            return 1;
    return 0;
}

static void mark_clear(Pane *p) { p->nmark = 0; }

static void mark_toggle(Pane *p, const char *path)
{
    int i;
    if (!strcmp(p->ent[p->cur].name, ".."))
        return;
    for (i = 0; i < p->nmark; i++) {
        if (!strcmp(p->marks[i], path)) {
            memmove(p->marks[i], p->marks[i + 1], (p->nmark - i - 1) * PATHLEN);
            p->nmark--;
            return;
        }
    }
    if (p->nmark < MAXMARK)
        snprintf(p->marks[p->nmark++], PATHLEN, "%s", path);
}

static int cmp_ent(const void *a, const void *b, int sort)
{
    const Ent *x = a, *y = b;
    if (!strcmp(x->name, ".."))
        return -1;
    if (!strcmp(y->name, ".."))
        return 1;
    if (x->isdir != y->isdir)
        return y->isdir - x->isdir;
    if (sort == SORT_SIZE) {
        if (x->size != y->size)
            return x->size < y->size ? 1 : -1;
    } else if (sort == SORT_DATE) {
        if (x->mtime != y->mtime)
            return x->mtime < y->mtime ? 1 : -1;
    }
    return strcasecmp(x->name, y->name);
}

static int cmp_name(const void *a, const void *b) { return cmp_ent(a, b, SORT_NAME); }
static int cmp_size(const void *a, const void *b) { return cmp_ent(a, b, SORT_SIZE); }
static int cmp_date(const void *a, const void *b) { return cmp_ent(a, b, SORT_DATE); }

static void pane_reload(Pane *p)
{
    DIR *d;
    struct dirent *de;
    struct stat st;
    char keep[PATHLEN] = "";
    char full[PATHLEN];
    int i;

    if (p->n && p->cur >= 0 && p->cur < p->n)
        snprintf(keep, sizeof keep, "%s", p->ent[p->cur].path);

    p->n = 0;
    snprintf(p->ent[0].name, NAMELEN, "..");
    if (strcmp(p->path, "/")) {
        char *slash = strrchr(p->path, '/');
        if (slash == p->path)
            snprintf(p->ent[0].path, PATHLEN, "/");
        else {
            snprintf(p->ent[0].path, PATHLEN, "%s", p->path);
            slash = strrchr(p->ent[0].path, '/');
            if (slash && slash != p->ent[0].path)
                *slash = 0;
            else
                snprintf(p->ent[0].path, PATHLEN, "/");
        }
    } else
        snprintf(p->ent[0].path, PATHLEN, "/");
    p->ent[0].isdir = 1;
    p->ent[0].size = 0;
    p->ent[0].mtime = 0;
    p->n = 1;

    d = opendir(p->path);
    if (d) {
        while ((de = readdir(d)) && p->n < MAXENT) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                continue;
            if (!p->hidden && de->d_name[0] == '.')
                continue;
            if (p->filter[0]) {
                char low[NAMELEN], f[64];
                int k;
                snprintf(low, sizeof low, "%s", de->d_name);
                snprintf(f, sizeof f, "%s", p->filter);
                for (k = 0; low[k]; k++)
                    low[k] = tolower((unsigned char)low[k]);
                for (k = 0; f[k]; k++)
                    f[k] = tolower((unsigned char)f[k]);
                if (!strstr(low, f))
                    continue;
            }
            snprintf(full, sizeof full, "%s/%s", strcmp(p->path, "/") ? p->path : "", de->d_name);
            if (lstat(full, &st) != 0)
                continue;
            snprintf(p->ent[p->n].path, PATHLEN, "%s", full);
            if (S_ISDIR(st.st_mode))
                snprintf(p->ent[p->n].name, NAMELEN, "%s/", de->d_name);
            else
                snprintf(p->ent[p->n].name, NAMELEN, "%s", de->d_name);
            p->ent[p->n].isdir = S_ISDIR(st.st_mode);
            p->ent[p->n].size = S_ISDIR(st.st_mode) ? 0 : st.st_size;
            p->ent[p->n].mtime = st.st_mtime;
            p->n++;
        }
        closedir(d);
    }
    if (p->sort == SORT_SIZE)
        qsort(p->ent + 1, p->n > 0 ? p->n - 1 : 0, sizeof(Ent), cmp_size);
    else if (p->sort == SORT_DATE)
        qsort(p->ent + 1, p->n > 0 ? p->n - 1 : 0, sizeof(Ent), cmp_date);
    else
        qsort(p->ent + 1, p->n > 0 ? p->n - 1 : 0, sizeof(Ent), cmp_name);

    p->cur = 0;
    if (keep[0]) {
        for (i = 0; i < p->n; i++)
            if (!strcmp(p->ent[i].path, keep))
                p->cur = i;
    }
    if (p->cur >= p->n)
        p->cur = p->n ? p->n - 1 : 0;
}

static Ent *cur(Pane *p)
{
    if (!p->n)
        return NULL;
    return &p->ent[p->cur];
}

static void pane_enter(Pane *p)
{
    Ent *e = cur(p);
    if (!e || !e->isdir)
        return;
    abspath_into(p->path, PATHLEN, e->path);
    p->cur = p->scroll = 0;
    p->filter[0] = 0;
    mark_clear(p);
    pane_reload(p);
}

static void pane_parent(Pane *p)
{
    char old[PATHLEN];
    char *slash;
    int i;
    snprintf(old, sizeof old, "%s", p->path);
    if (!strcmp(p->path, "/"))
        return;
    slash = strrchr(p->path, '/');
    if (slash == p->path)
        p->path[1] = 0;
    else if (slash)
        *slash = 0;
    p->filter[0] = 0;
    pane_reload(p);
    for (i = 0; i < p->n; i++)
        if (!strcmp(p->ent[i].path, old))
            p->cur = i;
}

static void pane_jump(Pane *p, const char *path)
{
    struct stat st;
    if (stat(path, &st) || !S_ISDIR(st.st_mode))
        return;
    abspath_into(p->path, PATHLEN, path);
    p->cur = p->scroll = 0;
    p->filter[0] = 0;
    mark_clear(p);
    pane_reload(p);
}

static void pane_move(Pane *p, int d)
{
    if (!p->n)
        return;
    p->cur += d;
    if (p->cur < 0)
        p->cur = 0;
    if (p->cur >= p->n)
        p->cur = p->n - 1;
}

static int collect_sel(Pane *p, Ent **out, int max)
{
    int i, n = 0;
    for (i = 0; i < p->n; i++) {
        if (marked(p, p->ent[i].path) && strcmp(p->ent[i].name, "..")) {
            if (n < max)
                out[n++] = &p->ent[i];
        }
    }
    if (n)
        return n;
    if (p->n && strcmp(p->ent[p->cur].name, "..")) {
        out[0] = &p->ent[p->cur];
        return 1;
    }
    return 0;
}

static const char *fmt_size(off_t n, char *b, size_t nb)
{
    if (n < 1024)
        snprintf(b, nb, "%ldB", (long)n);
    else if (n < 1024 * 1024)
        snprintf(b, nb, "%ldK", (long)(n / 1024));
    else
        snprintf(b, nb, "%.1fM", n / (1024.0 * 1024.0));
    return b;
}

static void disk_free(const char *path, char *b, size_t n)
{
    FILE *f = NULL;
    (void)path;
    snprintf(b, n, "");
    f = popen("df -h . 2>/dev/null | tail -1", "r");
    if (f) {
        char line[256], used[32], tot[32], av[32];
        if (fgets(line, sizeof line, f)) {
            if (sscanf(line, "%*s %31s %31s %31s", tot, used, av) >= 3)
                snprintf(b, n, "free %s/%s", av, tot);
        }
        pclose(f);
    }
}

static int prompt_box(const char *title, const char *def, char *out, size_t n)
{
    int h, w, ph = 5, pw, y, x;
    WINDOW *win;
    getmaxyx(scr, h, w);
    pw = w - 2 < 48 ? w - 2 : 48;
    if (pw < 20)
        pw = w > 4 ? w - 2 : 20;
    y = h / 2 - 2;
    if (y < 0)
        y = 0;
    x = (w - pw) / 2;
    if (x < 0)
        x = 0;
    win = newwin(ph, pw, y, x);
    wbkgd(win, COLOR_PAIR(4));
    box(win, 0, 0);
    mvwaddnstr(win, 1, 2, title, pw - 4);
    echo();
    curs_set(1);
    wmove(win, 3, 2);
    if (def && *def)
        mvwaddnstr(win, 3, 2, def, pw - 4);
    wmove(win, 3, 2);
    wgetnstr(win, out, (int)n - 1);
    curs_set(0);
    noecho();
    delwin(win);
    if (!out[0] && def)
        snprintf(out, n, "%s", def);
    return out[0] != 0;
}

static int confirm(const char *title)
{
    char buf[8] = "n";
    prompt_box(title, "n", buf, sizeof buf);
    return buf[0] == 'y' || buf[0] == 'Y';
}

static int pick_list(const char *title, char rows[][128], int n)
{
    int h, w, ph, pw, y, x, cur = 0, k, i;
    if (n <= 0)
        return -1;
    getmaxyx(scr, h, w);
    ph = n + 3;
    if (ph > h - 2)
        ph = h - 2;
    pw = 40;
    if (pw > w - 2)
        pw = w - 2;
    y = (h - ph) / 2;
    x = (w - pw) / 2;
    if (y < 0)
        y = 0;
    if (x < 0)
        x = 0;
    for (;;) {
        WINDOW *win = newwin(ph, pw, y, x);
        int vis = ph - 3, top;
        wbkgd(win, COLOR_PAIR(4));
        box(win, 0, 0);
        mvwaddnstr(win, 1, 2, title, pw - 4);
        top = cur - vis + 1;
        if (top < 0)
            top = 0;
        for (i = 0; i < vis && top + i < n; i++) {
            if (top + i == cur)
                wattron(win, A_REVERSE);
            mvwaddnstr(win, 2 + i, 2, rows[top + i], pw - 4);
            wattroff(win, A_REVERSE);
        }
        wrefresh(win);
        k = wgetch(win);
        delwin(win);
        if (k == 27 || k == 'q')
            return -1;
        if (k == KEY_UP && cur)
            cur--;
        if (k == KEY_DOWN && cur < n - 1)
            cur++;
        if (k == 10 || k == 13 || k == KEY_ENTER)
            return cur;
    }
}

static void run_external(const char *cmd)
{
    def_prog_mode();
    endwin();
    printf("\n%s\n", cmd);
    fflush(stdout);
    system(cmd);
    printf("\n[enter]");
    fflush(stdout);
    getchar();
    reset_prog_mode();
    refresh();
}

static int copy_file(const char *src, const char *dst)
{
    char buf[8192];
    ssize_t n;
    int in, out;
    in = open(src, O_RDONLY);
    if (in < 0)
        return -1;
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        close(in);
        return -1;
    }
    while ((n = read(in, buf, sizeof buf)) > 0) {
        if (write(out, buf, (size_t)n) != n) {
            close(in);
            close(out);
            return -1;
        }
    }
    close(in);
    close(out);
    return n < 0 ? -1 : 0;
}

static int g_copy_err;
static const char *g_copy_src;
static const char *g_copy_dst;

static int copy_walk(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    char dest[PATHLEN];
    const char *rel = fpath + strlen(g_copy_src);
    if (*rel == '/')
        rel++;
    if (*rel)
        snprintf(dest, sizeof dest, "%s/%s", g_copy_dst, rel);
    else
        snprintf(dest, sizeof dest, "%s", g_copy_dst);
    (void)ftwbuf;
    if (typeflag == FTW_D) {
        if (mkdir(dest, sb->st_mode & 0777) && errno != EEXIST)
            g_copy_err = 1;
    } else if (typeflag == FTW_F) {
        if (copy_file(fpath, dest))
            g_copy_err = 1;
    }
    return 0;
}

static int copy_tree(const char *src, const char *dst)
{
    g_copy_err = 0;
    g_copy_src = src;
    g_copy_dst = dst;
    mkdir(dst, 0755);
    nftw(src, copy_walk, 16, 0);
    return g_copy_err ? -1 : 0;
}

static int rm_walk(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    (void)sb;
    (void)ftwbuf;
    if (typeflag == FTW_DP || typeflag == FTW_D)
        return rmdir(fpath);
    return unlink(fpath);
}

static int rm_path(const char *path, int isdir)
{
    if (isdir)
        return nftw(path, rm_walk, 16, FTW_DEPTH | FTW_PHYS);
    return unlink(path);
}

static void basename_of(const char *path, char *out, size_t n)
{
    const char *s = strrchr(path, '/');
    snprintf(out, n, "%s", s && s[1] ? s + 1 : path);
    if (out[0] && out[strlen(out) - 1] == '/')
        out[strlen(out) - 1] = 0;
}

static void do_copy(int move, int same)
{
    Pane *p = &panes[active], *o = &panes[1 - active];
    Ent *sel[MAXMARK];
    int ns = collect_sel(p, sel, MAXMARK);
    char dest_dir[PATHLEN], dest_name[NAMELEN] = "", dest[PATHLEN], base[NAMELEN];
    int i, ok = 0;
    const char *verb = move ? "move" : "copy";

    if (!ns) {
        say("nothing selected", 0);
        return;
    }
    snprintf(dest_dir, sizeof dest_dir, "%s", same ? p->path : o->path);
    if (same || ns == 1) {
        basename_of(sel[0]->path, base, sizeof base);
        if (!prompt_box(verb, base, dest_name, sizeof dest_name)) {
            say("cancelled", 0);
            return;
        }
    } else {
        char q[128];
        snprintf(q, sizeof q, "%s %d -> other?", verb, ns);
        if (!confirm(q)) {
            say("cancelled", 0);
            return;
        }
    }
    for (i = 0; i < ns; i++) {
        basename_of(sel[i]->path, base, sizeof base);
        snprintf(dest, sizeof dest, "%s/%s", dest_dir, ns == 1 && dest_name[0] ? dest_name : base);
        if (!strcmp(sel[i]->path, dest))
            continue;
        if (!access(dest, F_OK)) {
            char q[192];
            snprintf(q, sizeof q, "overwrite %s?", base);
            if (!confirm(q))
                continue;
            rm_path(dest, sel[i]->isdir);
        }
        if (move) {
            if (rename(sel[i]->path, dest) != 0) {
                if (sel[i]->isdir)
                    copy_tree(sel[i]->path, dest);
                else
                    copy_file(sel[i]->path, dest);
                rm_path(sel[i]->path, sel[i]->isdir);
            }
        } else if (sel[i]->isdir) {
            if (copy_tree(sel[i]->path, dest)) {
                say(strerror(errno), 1);
                break;
            }
        } else if (copy_file(sel[i]->path, dest)) {
            say(strerror(errno), 1);
            break;
        }
        ok++;
    }
    mark_clear(p);
    pane_reload(p);
    pane_reload(o);
    if (ok) {
        char b[64];
        snprintf(b, sizeof b, "%s %d", verb, ok);
        say(b, 0);
    }
}

static void do_mkdir(void)
{
    char name[NAMELEN], dest[PATHLEN];
    if (!prompt_box("mkdir", "", name, sizeof name))
        return;
    snprintf(dest, sizeof dest, "%s/%s", panes[active].path, name);
    if (mkdir(dest, 0755))
        say(strerror(errno), 1);
    else {
        pane_reload(&panes[active]);
        say("made dir", 0);
    }
}

static void do_rename(void)
{
    Ent *e = cur(&panes[active]);
    char name[NAMELEN], dest[PATHLEN], old[NAMELEN];
    if (!e || !strcmp(e->name, ".."))
        return;
    basename_of(e->path, old, sizeof old);
    if (!prompt_box("rename", old, name, sizeof name) || !strcmp(name, old))
        return;
    snprintf(dest, sizeof dest, "%s/%s", panes[active].path, name);
    if (rename(e->path, dest))
        say(strerror(errno), 1);
    else {
        pane_reload(&panes[active]);
        say("renamed", 0);
    }
}

static void do_delete(void)
{
    Ent *sel[MAXMARK];
    int ns = collect_sel(&panes[active], sel, MAXMARK);
    char q[64];
    int i, ok = 0;
    if (!ns) {
        say("nothing selected", 0);
        return;
    }
    snprintf(q, sizeof q, "delete %d?", ns);
    if (!confirm(q)) {
        say("cancelled", 0);
        return;
    }
    for (i = 0; i < ns; i++) {
        if (rm_path(sel[i]->path, sel[i]->isdir)) {
            say(strerror(errno), 1);
            break;
        }
        ok++;
    }
    mark_clear(&panes[active]);
    pane_reload(&panes[active]);
    if (ok) {
        snprintf(q, sizeof q, "deleted %d", ok);
        say(q, 0);
    }
}

static void do_chmodx(void)
{
    Ent *sel[MAXMARK];
    int ns = collect_sel(&panes[active], sel, MAXMARK);
    int i, ok = 0;
    for (i = 0; i < ns; i++) {
        struct stat st;
        if (stat(sel[i]->path, &st) == 0 &&
            chmod(sel[i]->path, st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) == 0)
            ok++;
    }
    {
        char b[32];
        snprintf(b, sizeof b, "+x %d", ok);
        say(b, 0);
    }
    pane_reload(&panes[active]);
}

static void do_symlink(void)
{
    Ent *sel[MAXMARK];
    int ns = collect_sel(&panes[active], sel, MAXMARK);
    int i, ok = 0;
    char dest[PATHLEN], base[NAMELEN];
    for (i = 0; i < ns; i++) {
        basename_of(sel[i]->path, base, sizeof base);
        snprintf(dest, sizeof dest, "%s/%s", panes[1 - active].path, base);
        if (!access(dest, F_OK)) {
            char q[192];
            snprintf(q, sizeof q, "overwrite %s?", base);
            if (!confirm(q))
                continue;
            unlink(dest);
        }
        if (symlink(sel[i]->path, dest) == 0)
            ok++;
        else {
            say(strerror(errno), 1);
            break;
        }
    }
    pane_reload(&panes[1 - active]);
    {
        char b[32];
        snprintf(b, sizeof b, "link %d", ok);
        say(b, 0);
    }
}

static void view_file(const char *path)
{
    FILE *f;
    char lines[400][200];
    int n = 0, top = 0, k, h, w, i;
    f = fopen(path, "r");
    if (!f) {
        say("cannot open", 1);
        return;
    }
    while (n < 400 && fgets(lines[n], sizeof lines[0], f)) {
        lines[n][strcspn(lines[n], "\n")] = 0;
        n++;
    }
    fclose(f);
    for (;;) {
        getmaxyx(scr, h, w);
        erase();
        put(0, 0, path, w, A_REVERSE);
        for (i = 1; i < h - 1; i++) {
            int li = top + i - 1;
            if (li < n)
                put(i, 0, lines[li], w, A_NORMAL);
        }
        put(h - 1, 0, "q back", w, A_REVERSE);
        refresh();
        k = getch();
        if (k == 'q' || k == 27 || k == KEY_F(10))
            break;
        if (k == KEY_UP && top)
            top--;
        if (k == KEY_DOWN && top + h - 2 < n)
            top++;
        if (k == KEY_NPAGE)
            top += h - 4;
        if (k == KEY_PPAGE)
            top -= h - 4;
        if (top < 0)
            top = 0;
    }
}

static void edit_file(void)
{
    Ent *e = cur(&panes[active]);
    char cmd[PATHLEN + 32];
    const char *ed;
    if (!e || e->isdir) {
        say("not a file", 0);
        return;
    }
    ed = getenv("EDITOR");
    if (!ed)
        ed = "nano";
    snprintf(cmd, sizeof cmd, "%s '%s'", ed, e->path);
    run_external(cmd);
    pane_reload(&panes[active]);
}

static void help_screen(void)
{
    static const char *lines[] = {
        "Tab        other pane",
        "Enter      open dir / view file",
        "Backspace  parent",
        "Space      mark",
        "h          hidden",
        "/          filter",
        "j          jumps",
        "s          sort name/size/date",
        "p          preview other pane",
        "x          chmod +x",
        "l          symlink into other pane",
        "c          copy in same pane",
        "F2 rename  F3 view  F4 edit",
        "F5 copy    F6 move  F7 mkdir",
        "F8 delete  F9 menu  F10 quit",
        NULL,
    };
    int i, k, h, w;
    for (;;) {
        getmaxyx(scr, h, w);
        erase();
        put(0, 0, "pCMD help", w, A_REVERSE);
        for (i = 0; lines[i] && i + 1 < h - 1; i++)
            put(i + 1, 0, lines[i], w, A_NORMAL);
        put(h - 1, 0, "q back", w, A_REVERSE);
        refresh();
        k = getch();
        if (k == 'q' || k == 27 || k == KEY_F(1) || k == KEY_F(10))
            break;
    }
}

static int load_kv(const char *path, char labels[][128], char vals[][PATHLEN], int max)
{
    FILE *f;
    char line[512];
    int n = 0;
    f = fopen(path, "r");
    if (!f)
        return 0;
    while (fgets(line, sizeof line, f) && n < max) {
        char *bar;
        line[strcspn(line, "\n")] = 0;
        if (!line[0] || line[0] == '#')
            continue;
        bar = strchr(line, '|');
        if (!bar)
            continue;
        *bar++ = 0;
        snprintf(labels[n], 128, "%s", line);
        snprintf(vals[n], PATHLEN, "%s", bar);
        n++;
    }
    fclose(f);
    return n;
}

static void expand_cmd(const char *tmpl, char *out, size_t n)
{
    Pane *p = &panes[active], *o = &panes[1 - active];
    Ent *e = cur(p);
    const char *file = e ? e->path : p->path;
    const char *base = e ? e->name : "";
    size_t i = 0;
    while (*tmpl && i + 1 < n) {
        if (tmpl[0] == '%' && tmpl[1]) {
            const char *rep = "";
            if (tmpl[1] == 'p')
                rep = p->path;
            else if (tmpl[1] == 'd')
                rep = o->path;
            else if (tmpl[1] == 'f')
                rep = file;
            else if (tmpl[1] == 'n')
                rep = base;
            while (*rep && i + 1 < n)
                out[i++] = *rep++;
            tmpl += 2;
        } else
            out[i++] = *tmpl++;
    }
    out[i] = 0;
}

static char *home_path(const char *name, char *out, size_t n)
{
    const char *h = getenv("HOME");
    if (!h)
        h = ".";
    snprintf(out, n, "%s/%s", h, name);
    return out;
}

static void user_menu(void)
{
    char path[PATHLEN], labels[32][128], cmds[32][PATHLEN], run[PATHLEN];
    int n, i;
    home_path(".pcmd.menu", path, sizeof path);
    n = load_kv(path, labels, cmds, 32);
    if (!n) {
        home_path(".scmd.menu", path, sizeof path);
        n = load_kv(path, labels, cmds, 32);
    }
    if (!n) {
        say("no ~/.pcmd.menu", 0);
        return;
    }
    i = pick_list("menu", labels, n);
    if (i < 0)
        return;
    expand_cmd(cmds[i], run, sizeof run);
    run_external(run);
}

static void jump_menu(void)
{
    char path[PATHLEN], labels[32][128], dests[32][PATHLEN];
    int n, i;
    home_path(".pcmd.jumps", path, sizeof path);
    n = load_kv(path, labels, dests, 32);
    if (!n) {
        home_path(".scmd.jumps", path, sizeof path);
        n = load_kv(path, labels, dests, 32);
    }
    if (!n) {
        const char *defs[][2] = {
            {"home", NULL},
            {"work", "/home/working"},
            {"tmp", "/tmp"},
            {"root", "/"},
            {NULL, NULL},
        };
        n = 0;
        for (i = 0; defs[i][0]; i++) {
            const char *d = defs[i][1];
            snprintf(labels[n], 128, "%s", defs[i][0]);
            if (!d)
                snprintf(dests[n], PATHLEN, "%s", getenv("HOME") ? getenv("HOME") : "/");
            else
                snprintf(dests[n], PATHLEN, "%s", d);
            n++;
        }
    }
    i = pick_list("jump", labels, n);
    if (i >= 0)
        pane_jump(&panes[active], dests[i]);
}

static void preview_into(const char *path, int y0, int x0, int h, int w)
{
    FILE *f;
    char line[256];
    int row = 0;
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        put(y0, x0, "<DIR>", w, COLOR_PAIR(3));
        return;
    }
    f = fopen(path, "r");
    if (!f) {
        put(y0, x0, "<unreadable>", w, COLOR_PAIR(3));
        return;
    }
    while (row < h && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        put(y0 + row, x0, line, w, COLOR_PAIR(3));
        row++;
    }
    fclose(f);
}

static void draw(void)
{
    int h, w, mid, list_h, i, row;
    char line[512], sz[16], when[32], info[256], df[64];
    Pane *p;
    Ent *e;
    getmaxyx(scr, h, w);
    mid = w / 2;
    if (mid < 10)
        mid = 10;
    list_h = h - 4;
    if (list_h < 3)
        list_h = 3;
    erase();
    for (i = 0; i < 2; i++) {
        int x0 = i ? mid : 0;
        int pw = i ? w - mid : mid;
        int act = (i == active);
        p = &panes[i];
        snprintf(line, sizeof line, "%s%s %s", p->path, p->filter[0] ? " /" : "",
                 p->sort == SORT_SIZE ? "size" : p->sort == SORT_DATE ? "date" : "name");
        put(0, x0, line, pw, COLOR_PAIR(1) | (act ? A_BOLD : 0));
        if (preview && i != active) {
            Ent *ce = cur(&panes[active]);
            preview_into(ce ? ce->path : p->path, 1, x0, list_h, pw);
            continue;
        }
        if (p->cur < p->scroll)
            p->scroll = p->cur;
        if (p->cur >= p->scroll + list_h)
            p->scroll = p->cur - list_h + 1;
        if (p->scroll < 0)
            p->scroll = 0;
        for (row = 0; row < list_h; row++) {
            int idx = p->scroll + row;
            int attr = A_NORMAL;
            if (idx >= p->n)
                break;
            e = &p->ent[idx];
            if (e->isdir)
                snprintf(sz, sizeof sz, "<DIR>");
            else
                fmt_size(e->size, sz, sizeof sz);
            snprintf(line, sizeof line, "%c%s", marked(p, e->path) ? '*' : ' ', e->name);
            if (idx == p->cur && act)
                attr = COLOR_PAIR(2) | A_BOLD;
            else if (e->isdir)
                attr = COLOR_PAIR(3);
            put(1 + row, x0, line, pw - 7, attr);
            put(1 + row, x0 + pw - 6, sz, 6, attr);
        }
    }
    if (mid > 0 && mid < w) {
        for (row = 1; row <= list_h; row++)
            mvaddch(row, mid, ACS_VLINE);
    }
    p = &panes[active];
    e = cur(p);
    when[0] = 0;
    if (e && e->mtime) {
        struct tm *tm = localtime(&e->mtime);
        strftime(when, sizeof when, "%m-%d %H:%M", tm);
    }
    disk_free(p->path, df, sizeof df);
    snprintf(info, sizeof info, "%s %s *%d %s", e ? e->name : "", when, p->nmark, df);
    put(h - 3, 0, info, w, COLOR_PAIR(4));
    put(h - 2, 0, msg[0] ? msg : NAME, w, msg_err ? COLOR_PAIR(5) : COLOR_PAIR(4));
    put(h - 1, 0, "F1? F3 view F4 ed F5 cp F6 mv F7 md F8 rm F9 menu / j s p x l", w, A_REVERSE);
    refresh();
}

static void loop(void)
{
    int k;
    for (;;) {
        draw();
        k = getch();
        msg_err = 0;
        if (k == 'q' || k == KEY_F(10))
            break;
        if (k == KEY_F(1) || k == '?')
            help_screen();
        else if (k == 9)
            active = 1 - active;
        else if (k == KEY_UP)
            pane_move(&panes[active], -1);
        else if (k == KEY_DOWN)
            pane_move(&panes[active], 1);
        else if (k == KEY_PPAGE)
            pane_move(&panes[active], -(LINES - 4));
        else if (k == KEY_NPAGE)
            pane_move(&panes[active], LINES - 4);
        else if (k == KEY_HOME)
            panes[active].cur = 0;
        else if (k == KEY_END)
            panes[active].cur = panes[active].n ? panes[active].n - 1 : 0;
        else if (k == 10 || k == 13 || k == KEY_ENTER) {
            Ent *e = cur(&panes[active]);
            if (e && e->isdir)
                pane_enter(&panes[active]);
            else if (e)
                view_file(e->path);
        } else if (k == KEY_BACKSPACE || k == 127 || k == 8)
            pane_parent(&panes[active]);
        else if (k == ' ') {
            Ent *e = cur(&panes[active]);
            if (e)
                mark_toggle(&panes[active], e->path);
            pane_move(&panes[active], 1);
        } else if (k == 'h' || k == 'H') {
            panes[active].hidden ^= 1;
            pane_reload(&panes[active]);
        } else if (k == '/') {
            prompt_box("filter", panes[active].filter, panes[active].filter, sizeof panes[active].filter);
            panes[active].cur = 0;
            pane_reload(&panes[active]);
        } else if (k == 'j' || k == 'J')
            jump_menu();
        else if (k == 's' || k == 'S') {
            panes[active].sort = (panes[active].sort + 1) % 3;
            pane_reload(&panes[active]);
            say(panes[active].sort == SORT_SIZE ? "sort size" :
                panes[active].sort == SORT_DATE ? "sort date" : "sort name", 0);
        } else if (k == 'p' || k == 'P') {
            preview ^= 1;
            say(preview ? "preview on" : "preview off", 0);
        } else if (k == 'x' || k == 'X')
            do_chmodx();
        else if (k == 'l' || k == 'L')
            do_symlink();
        else if (k == 'c' || k == 'C')
            do_copy(0, 1);
        else if (k == KEY_F(2))
            do_rename();
        else if (k == KEY_F(3)) {
            Ent *e = cur(&panes[active]);
            if (e && e->isdir)
                pane_enter(&panes[active]);
            else if (e)
                view_file(e->path);
        } else if (k == KEY_F(4))
            edit_file();
        else if (k == KEY_F(5))
            do_copy(0, 0);
        else if (k == KEY_F(6))
            do_copy(1, 0);
        else if (k == KEY_F(7))
            do_mkdir();
        else if (k == KEY_F(8))
            do_delete();
        else if (k == KEY_F(9))
            user_menu();
    }
}

int main(int argc, char **argv)
{
    const char *left, *right;
    if (!isatty(1)) {
        fprintf(stderr, "pCMD needs a tty\n");
        return 2;
    }
    left = argc > 1 ? argv[1] : ".";
    right = "/home/working/SharkDeck/SharkDeck";
    if (access(right, F_OK) != 0)
        right = getenv("HOME") ? getenv("HOME") : ".";

    initscr();
    scr = stdscr;
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_WHITE, COLOR_BLUE);
        init_pair(2, COLOR_BLACK, COLOR_CYAN);
        init_pair(3, COLOR_YELLOW, COLOR_BLACK);
        init_pair(4, COLOR_WHITE, COLOR_BLUE);
        init_pair(5, COLOR_WHITE, COLOR_RED);
    }
    memset(panes, 0, sizeof panes);
    abspath_into(panes[0].path, PATHLEN, left);
    abspath_into(panes[1].path, PATHLEN, right);
    pane_reload(&panes[0]);
    pane_reload(&panes[1]);
    say("pCMD", 0);
    loop();
    endwin();
    return 0;
}
