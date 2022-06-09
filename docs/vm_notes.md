
for anon mappings - an anonymous vm_object references an underlying amap.
amap is an index of anon structures. amap has a refcount. if 1, normal semantics.
if >1, COW of the amap structure. anons also have a refcount, and if >1 COW on
them and their associated page

so we begin with vm_object 1 referencing amap 1 which has anons for each page
within the amap.

now we fork. child has new vm_object 2 created. references amap 1 causing amap1 to become COW.
mapped read-only.
vm_object 1 from parent also gets updated to become a read-only mapping.

when a read fault occurs - we can just map in the page from the amap's relevant anon entry, ++ anon's refcnt.
when a write fault occurs - then we check the amap's refcount. 
- if >1, we copy a new amap referring to the same anons - amap 2 - and decr amap1
refcnt. ++refcnt of all anons in amap.
- if 1, we need not copy.

now we identify in which anon the fault refers. and we check that anon's refcnt. it should be >1 if this is the first of the parent or child to try and do a write.
- if 1, we keep it around, map that page rw
- if >1, we make a new anon and copy it into our amap. -- old anon refcnt.

what to still think about:
- mapping vnodes
- swapping (presumably an anon should be able to identify either a physical page
 or refer to where a page has been swapped out to. do we want pagers to operate
 at the anon level? logically then vnodes are implemented in terms of a vnode-pager
 for anons.)