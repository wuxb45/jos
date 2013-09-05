#include <inc/lib.h>

struct workdesc {
  int x;
  int y;
  void (*op)(struct workdesc *);
  envid_t id;
};

void north_op(struct workdesc *);
void west_op(struct workdesc *);
void center_op(struct workdesc *);
void east_op(struct workdesc *);
void south_op(struct workdesc *);

static struct workdesc wdlist[] = {
  {0, 1, north_op, 0},
  {0, 2, north_op, 0},
  {0, 3, north_op, 0},

  {1, 0, west_op, 0},
  {2, 0, west_op, 0},
  {3, 0, west_op, 0},

  {1, 1, center_op, 0},
  {2, 1, center_op, 0},
  {3, 1, center_op, 0},
  {1, 2, center_op, 0},
  {2, 2, center_op, 0},
  {3, 2, center_op, 0},
  {1, 3, center_op, 0},
  {2, 3, center_op, 0},
  {3, 3, center_op, 0},

  {1, 4, east_op, 0},
  {2, 4, east_op, 0},
  {3, 4, east_op, 0},

  {4, 1, south_op, 0},
  {4, 2, south_op, 0},
  {4, 3, south_op, 0},
};

static uint32_t ready = 0;

static int MA[3][3] = {{2, 3, 7},
                       {5, 11, 19},
                       {13, 23, 29}};

static int MB[3][3] = {{97, 91, 73},
                       {61, 17, 37},
                       {83, 47, 41}};
static int MC[3][3];

struct workdesc *
find_wd_by_xy(const int x, const int y)
{
  int i;
  for (i = 0; i < 21; i++) {
    if ((wdlist[i].x == x) && (wdlist[i].y == y)) {
      return &(wdlist[i]);
    }
  }
  return NULL;
}

envid_t
find_envid_by_xy(const int x, const int y)
{
  struct workdesc * wd = find_wd_by_xy(x, y);
  if (wd) {
    return wd->id;
  } else {
    return 0;
  }
}

void
print_env_info(const envid_t envid)
{
  int i;
  for (i = 0; i < 21; i++) {
    if (wdlist[i].id == envid) {
      cprintf("[%d, %d], %d\n", wdlist[i].x, wdlist[i].y, wdlist[i].id);
      return;
    }
  }
}

envid_t
find_north(struct workdesc *wd)
{
  return find_envid_by_xy(wd->x - 1, wd->y);
}

envid_t
find_south(struct workdesc *wd)
{
  return find_envid_by_xy(wd->x + 1, wd->y);
}

envid_t
find_west(struct workdesc *wd)
{
  return find_envid_by_xy(wd->x, wd->y - 1);
}

envid_t
find_east(struct workdesc *wd)
{
  return find_envid_by_xy(wd->x, wd->y + 1);
}

void
north_op(struct workdesc *wd)
{
  const envid_t ids = find_south(wd);
  int i;
  for (i = 0; i < 3; i++) {
    ipc_send_alt(ids, 0, NULL, 0);
    const envid_t rid = ipc_recv_alt(NULL, NULL, NULL);
    assert(rid == ids);
  }
}

void
west_op(struct workdesc *wd)
{
  const envid_t ide = find_east(wd);
  int i;
  for (i = 0; i < 3; i++) {
    //cprintf("w[%d] send out %d\n", wd->x, MB[i][wd->x-1]);
    ipc_send_alt(ide, MB[i][wd->x-1], NULL, 0);
    const envid_t rid = ipc_recv_alt(NULL, NULL, NULL);
    assert(rid == ide);
  }
}

void
center_op(struct workdesc *wd)
{
  const envid_t idn = find_north(wd);
  const envid_t ids = find_south(wd);
  const envid_t ide = find_east(wd);
  const envid_t idw = find_west(wd);
  int i, vw, vn;
  envid_t from1, from2;
  
  for (i = 0; i < 3; i++) {
    vw = vn = 0;
    const int v1 = ipc_recv_alt(&from1, NULL, NULL);
    if (from1 == idw) {
      vw = v1;
    } else if (from1 == idn) {
      vn = v1;
    } else {
      print_env_info(from1);
      panic("[%d,%d] recv not expected", wd->x, wd->y);
    }
    const int v2 = ipc_recv_alt(&from2, NULL, NULL);
    if (from1 == from2) {
      panic("from1 == from2, %d %d", from1, from2);
    }
    if (from2 == idw) {
      vw = v2;
    } else if (from2 == idn) {
      vn = v2;
    } else {
      print_env_info(from2);
      panic("[%d,%d] recv not expected", wd->x, wd->y);
    }
    //cprintf("[%d,%d]: n:%d, w:%d -> %d * %d + %d\n", wd->x, wd->y, vn, vw, vw, MA[wd->x-1][wd->y-1], vn);
    const int vt = vw * MA[wd->x-1][wd->y-1] + vn;
    ipc_send_alt(ide, vw, NULL, 0);
    ipc_send_alt(ids, vt, NULL, 0);
    ipc_recv_alt(NULL, NULL, NULL);
    ipc_recv_alt(NULL, NULL, NULL);
    // at last, send feedback
    ipc_send_alt(idn, wd->id, NULL, 0);
    ipc_send_alt(idw, wd->id, NULL, 0);
  }
}

void
east_op(struct workdesc *wd)
{
  const envid_t idw = find_west(wd);
  int i;
  for (i = 0; i < 3; i++) {
    (void)ipc_recv_alt(NULL, NULL, NULL);
    (void)ipc_send_alt(idw, 0, NULL, 0);
  }
}

void
south_op(struct workdesc *wd)
{
  const envid_t idn = find_north(wd);
  envid_t idr;
  int i, r;
  for (i = 0; i < 3; i++) {
    r = ipc_recv_alt(&idr, NULL, NULL);
    assert(idr == idn);
    ipc_send_alt(idn, 0, NULL, 0);
    MC[i][wd->y-1] = r;
  }
  const int y = wd->y -1;
  cprintf("MC[*][%d] = %d %d %d\n", y, MC[0][y], MC[1][y], MC[2][y]);
}

void
worker(void)
{
  const envid_t myid = sys_getenvid();
  struct workdesc *wd = NULL;
  int i;
  // find my wd
  for (i = 0; i < 21; i++) {
    if (wdlist[i].id == myid) {
      wd = &wdlist[i];
      break;
    }
  }
  wd->op(wd);
}

void
umain(int argc, char ** argv)
{
  // build a shared memory.
  int i, j;
  for (i = 0; i < 3; i++) {
    for (j = 0; j < 3; j++) {
      const int v = MA[0][i] * MB[j][0] + MA[1][i] * MB[j][1] + MA[2][i] * MB[j][2];
      cprintf("%d ", v);
    }
    cprintf("\n");
  }
  for (i = 0; i < 21; i++) {
    const envid_t cid = sfork();
    if (cid < 0) {
      panic("CID < 0");
    } else if (cid == 0) {
      // wait for init message
      while(ready == 0) sys_yield();
      worker();
      return;
    } else {
      wdlist[i].id = cid;
    }
  }
  ready = 1;
  return;
}
