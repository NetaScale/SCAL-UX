#ifndef SPL_H_
#define SPL_H_

typedef int spl_t;

static inline spl_t splhigh() { return 0; }
static inline spl_t splx(spl_t spl) { return spl; }

#endif /* SPL_H_ */
