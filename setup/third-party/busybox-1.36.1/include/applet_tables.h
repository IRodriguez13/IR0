/* This is a generated file, don't edit */

#define NUM_APPLETS 17
#define KNOWN_APPNAME_OFFSETS 0

const char applet_names[] ALIGN1 = ""
"ash" "\0"
"cat" "\0"
"cp" "\0"
"echo" "\0"
"false" "\0"
"grep" "\0"
"head" "\0"
"ls" "\0"
"mkdir" "\0"
"mv" "\0"
"pwd" "\0"
"rm" "\0"
"rmdir" "\0"
"sh" "\0"
"tail" "\0"
"touch" "\0"
"true" "\0"
;

#define APPLET_NO_ash 0
#define APPLET_NO_cat 1
#define APPLET_NO_cp 2
#define APPLET_NO_echo 3
#define APPLET_NO_false 4
#define APPLET_NO_grep 5
#define APPLET_NO_head 6
#define APPLET_NO_ls 7
#define APPLET_NO_mkdir 8
#define APPLET_NO_mv 9
#define APPLET_NO_pwd 10
#define APPLET_NO_rm 11
#define APPLET_NO_rmdir 12
#define APPLET_NO_sh 13
#define APPLET_NO_tail 14
#define APPLET_NO_touch 15
#define APPLET_NO_true 16

#ifndef SKIP_applet_main
int (*const applet_main[])(int argc, char **argv) = {
ash_main,
cat_main,
cp_main,
echo_main,
false_main,
grep_main,
head_main,
ls_main,
mkdir_main,
mv_main,
pwd_main,
rm_main,
rmdir_main,
ash_main,
tail_main,
touch_main,
true_main,
};
#endif

const uint8_t applet_flags[] ALIGN1 = {
0xe0,
0xa3,
0xbb,
0xc3,
0x03,
};

