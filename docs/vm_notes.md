
Overview
--------

The SCAL/UX virtual memory manager is principally derived from the design of
NetBSD's UVM. This provides for a flexible and portable system. VM management
APIs are platform-independent, with abstract representation of address spaces,
while the `pmap` module handles the translation of that abstract representation
into the form demanded by a particular platform's memory management unit.

## Data Structures

A number of data structures play a role in the system. This is an overview of
some of them:

- `vm_page_t`: represents a physical page of usable memory. May be on the free
  page queue or may be held by a `vm_anon_t`, in which case it contains a queue
  to which all `vm_anon_t`s referring to it belong.
- `vm_anon_t`: represents a logical page of pageable memory, i.e. swap or vnode-
  backed. It may refer to either a `vm_page_t`, or it may have been put back to
  its backing store, in which case it contains 2 words which are used by its
  used by its pager to identify how to retrieve it from the backing store. It
  contains a queue to which all `vm_amap_entry_t`s referring to it belong.
- `vm_amap_entry_t` - represents an entry within a `vm_amap_t`. Refers to a
  `vm_anon_t` and an offset, indexing the page offset within the `vm_object_t`
  to which this entry belongs.
- `vm_object_t` - a mappable object. It may be anonymous/vnode, in which it
  contains `vm_amap_entry_t`s, or special, in which case it simply contains a
  physical address at which the mapping begins. Also carries a size, and a queue
  to which all `vm_map_entry_t`s mapping the object belong.
- `vm_map_entry_t` - an entry within an address space map. Refers to a
  `vm_object_t`, with an offset into that object and a size. Also carries a
  back pointer to the `vm_map_t` in which it is mapped.
- `vm_map_t` - represents a virtual address space, composed of `vm_map_entry_t`s


Anonymous/vnode mappings
------------------------

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

questions:
- swapping (presumably an anon should be able to identify either a physical page
 or refer to where a page has been swapped out to. do we want pagers to operate
 at the anon level? logically then vnodes are implemented in terms of a vnode-pager
 for anon mappings.) --- answer: yes, this is what we will do.

notes:
 - pmap needs to be able to invalidate all PTEs across all maps mapping a
   particular physical address before we can implement putpage


Adaptations for map-local trimming
----------------------------------

- `vm_anon_t` would have to carry an "actually mapped" refcnt
- `vm_amap_entry_t` would have to carry a bool "is this page actually mapped into
  the address space"