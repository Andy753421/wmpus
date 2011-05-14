#include <stdlib.h>
#include <stdio.h>

#include "util.h"
#include "sys.h"
#include "wm.h"

int main(int argc, char **argv)
{
	win_t *root = sys_init();
	wm_init(root);
	sys_run(root);
	return 0;
}
