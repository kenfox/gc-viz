/*
 * This file and the rest of this project are under the MIT License.
 *
 * This program contains toy implementations of several different
 * garbage collector algorithms instrumented to produce nice
 * visualizations of how the algorithms work. Many corners were cut to
 * simplify the code so they are neither general purpose nor
 * efficient. It's not all smoke and mirrors, but where smoke and
 * mirrors worked, that's what I used.
 *
 * If you are trying to understand GC algorithms, it's best to read a
 * good introductory text such as the Jones Lins book and only look at
 * the visualizations this program generates until you understand the
 * algorithm.
 *
 * Ken Fox
 * August 2014
 */

#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <cstdio>
#include <set>
#include <map>

const int HeapSize = 2000;
const int HeapSemiSize = 1000;

const int ImageWordSize = 5;
const int ImageWidthInWords = 25;
const int ImageHeight = (HeapSize / ImageWidthInWords) * ImageWordSize;
const int ImageWidth = ImageWidthInWords * ImageWordSize;

typedef signed short SWd;
typedef unsigned short UWd;
typedef unsigned short Loc;
typedef void (*VisitFn)(Loc loc);

void log_alloc_mem(Loc loc, int size);
void log_free_mem(Loc loc, int size);
void log_init_obj(void *addr, const char *type);
void log_ref_count(Loc loc, int ref_count);
void log_ref_count(void *addr, int ref_count);
void log_get_val(const void *addr);
void log_set_val(void *addr, char val);
void log_set_val(void *addr, int val);
void log_set_ref(void *addr, Loc val);
void log_copy_mem(Loc to, Loc from, int size);
void log_copy_mem(void *to, void *from, int size);

// The Obj classes hold data values stored in Mem.heap.
// A custom type tagging system is used instead of C++ virtual
// because the heap is explicitly managed to demonstrate GC.
// All fields in Obj (or sub-type) must be sizeof(SWd). Hopefully
// the compiler will then layout objects so they map directly
// to an array of Swd. (struct equivalence runs deep...)

// WARNING! None of the value classes can allocate memory!
// If a collection occurs inside a value method, the object
// may move and cause memory corruption. Allocation must
// happen in the Ref classes and those classes must handle
// being moved.

class Obj {
  private:
    Obj(const Obj &rhs);
    Obj &operator=(const Obj &rhs);

  public:
    static const char *TypeName[];
    enum Type { TNil=0, TForward=1, TFree=2, TNum=3, TTup=4, TVec=5, TStr=6 };
    struct {
        UWd ref_count : 8;
        UWd mark : 1;
        UWd type : 4;
    } header;

    void init(Type type) {
        log_init_obj(&header, TypeName[type]);
        header.type = type;
        init_ref_count();
        header.mark = 0;
    }

    static Obj *at(Loc loc);

    Type type() const { return (Type)header.type; }

    void init_ref_count() {
#if REF_COUNT_GC
        header.ref_count = 1;
        log_ref_count(&header, header.ref_count);
#else
        header.ref_count = 0;
#endif
    }

    void inc_ref_count() {
#if REF_COUNT_GC
        header.ref_count += 1;
        log_ref_count(&header, header.ref_count);
#endif
    }

    bool dec_ref_count() {
#if REF_COUNT_GC
        header.ref_count -= 1;
        log_ref_count(&header, header.ref_count);
        if (header.ref_count == 0) {
            cleanup();
            return true;
        }
        else {
            return false;
        }
#else
        return false;
#endif
    }

    void traverse(VisitFn f) const;
    void fixup_references();
    void cleanup();
    UWd size() const;
    SWd to_i() const;
    bool equals(const Obj *that) const;
    void dump() const;
};

// The Ref classes represent pointers to data values stored in mem.
// A raw pointer must never be exposed in a place where a GC may
// happen because the C++ registers, stack and temporaries are not
// treated as roots.

// Refs never move because they are not allocated in Mem::heap,
// however, the loc value (which is a numeric offset from the start of
// the heap) in a Ref may change at any time.

class ObjRef {
  private:
    friend class Mem;

    ObjRef &operator=(const ObjRef &rhs);

    static ObjRef *root;
    ObjRef *prev;
    ObjRef *next;

    void add_to_root_set() {
        prev = 0;
        if (root) {
            root->prev = this;
            next = root;
        }
        else {
            next = 0;
        }
        root = this;
    }

  protected:
    enum RefType { ALLOC, COPY, SHARE };
    volatile Loc loc;

    ObjRef(RefType type, UWd loc_or_size, UWd new_size = 0);

  public:
    static ObjRef *nil;

    ObjRef(const ObjRef &that);

    ~ObjRef();

    Loc share();
    static ObjRef at(Loc loc) { return ObjRef(ObjRef::SHARE, loc); }
    static void unshare(Loc loc);

    Obj *referenced_Obj() const { return Obj::at(loc); }

    Obj::Type type() const { return referenced_Obj()->type(); }
    UWd size() const { return referenced_Obj()->size(); }
    SWd to_i() const { return referenced_Obj()->to_i(); }
    void dump() const { referenced_Obj()->dump(); }
    bool equals(ObjRef that) const {
        return referenced_Obj()->equals(that.referenced_Obj());
    }
};

// Pluggable memory management and GC algorithms.

class FreeBlock: public Obj {
  public:
    UWd len;
    UWd size() const { return len; }
};

class ForwardingAddress: public Obj {
  public:
    UWd to;
};

struct MemInfo {
    static uint time;

    bool is_allocated;
    bool is_overhead;
    uint last_write;
    uint last_read;

    MemInfo() {
        is_allocated = false;
        is_overhead = false;
        last_read = 0;
        last_write = 0;
    }

    void was_allocated() {
        is_allocated = true;
        is_overhead = false;
        last_read = 0;
        last_write = 0;
    }

    void was_freed() { is_allocated = false; }
    void was_read() { last_read = ++time; }
    void was_written() { last_write = ++time; is_overhead = false; }
    void was_overhead() { last_write = ++time; is_overhead = true; }
};

class Mem {
  public:
    // Real GC algorithms use unused heap space for marking the live
    // sets and storing forwarding addresses for moved objects.
    static std::map<Loc, Loc> forwarding;
    static std::set<Loc> live;
    static UWd heap[HeapSize];
    static MemInfo info[HeapSize]; // visualization info
    static Loc top;
    static Loc from_space;
    static Loc to_space;

    static Loc addr_to_loc(const void *addr) {
        Loc loc = ((char *)(addr) - (char *)heap) / sizeof(UWd);
        assert(loc < HeapSize);
        return loc;
    }

    // TODO: implement a first-fit algorithm instead of just the bump allocator.
    // free must add memory back to allocator, blocks should be coalesced

    static Loc reserve(UWd size) {
        Loc loc = top;
        top += size;
        assert(top < HeapSize);
        log_alloc_mem(loc, size);
        return loc;
    }

    static Loc reserve_with_possible_overlap(UWd size) {
        Loc loc = top;
        top += size;
        assert(top < HeapSize);
        return loc;
    }

    static Loc alloc(UWd size) {
        Loc loc = reserve(size);
        for (int i = 0; i < size; ++i) {
            heap[loc + i] = 0;
        }
        return loc;
    }

    static Loc copy(Loc from, UWd new_size = 0) {
        UWd size = Obj::at(from)->size();
        if (new_size > 0) {
            Loc to = reserve(new_size);
            UWd min = (new_size < size) ? new_size : size;
            for (int i = 0; i < min; ++i) {
                heap[to + i] = heap[from + i];
            }
            for (int i = min; i < new_size; ++i) {
                heap[to + i] = 0;
            }
            log_copy_mem(to, from, min);
            return to;
        }
        else {
            Loc to = reserve(size);
            for (int i = 0; i < size; ++i) {
                heap[to + i] = heap[from + i];
            }
            log_copy_mem(to, from, size);
            return to;
        }
    }

    static Loc move_without_forwarding(Loc from, UWd size) {
        Obj *from_obj = Obj::at(from);
        Loc to = reserve_with_possible_overlap(size);
        for (int i = 0; i < size; ++i) {
            heap[to + i] = heap[from + i];
        }
        log_copy_mem(to, from, size);
        return to;
    }

    static Loc move(Loc from) {
        Obj *from_obj = Obj::at(from);
        UWd size = from_obj->size();
        Loc to = reserve(size);
        for (int i = 0; i < size; ++i) {
            heap[to + i] = heap[from + i];
        }
        ForwardingAddress *b = (ForwardingAddress *)from_obj;
        b->init(Obj::TForward);
        b->to = to;
        log_copy_mem(to, from, size);
        return to;
    }

    static Loc read_barrier(Loc loc) {
        return loc;
    }

    static void free(Loc loc, int size) {
        FreeBlock *b = (FreeBlock *)Obj::at(loc);
        b->init(Obj::TFree);
        b->len = size;
        log_free_mem(loc, size);
    }

    static void mark_live_loc(Loc loc) {
        if (loc != 0) {
#if !COPY_GC
            log_ref_count(loc, 1); // treat marking as ref count for visualization
#endif
            live.insert(loc);
        }
    }

    static void mark_live() {
        ObjRef *p = ObjRef::root;
        live.clear();
        while (p) {
            Loc loc = p->loc;
            mark_live_loc(loc);
            Obj::at(loc)->traverse(mark_live_loc);
            p = p->next;
        }
    }

    static void sweep_garbage() {
        Loc loc = 1;
        while (loc < top) {
            Obj *obj = Obj::at(loc);
            int size = obj->size();
            if (live.count(loc) == 0) {
                free(loc, size);
            }
            loc += size;
        }
    }

    static void move_live() {
        mark_live();
        // nil is located at heap loc 0 and doesn't move
        top = (top >= HeapSemiSize) ? 1 : HeapSemiSize;
        std::set<Loc>::iterator it;
        for (it = live.begin(); it != live.end(); ++it) {
            Loc from = *it;
            if (from) {
                move(from);
            }
        }
    }

    static void compact_live() {
        forwarding.clear();
        mark_live();
        Loc old_top = top;
        Loc from = 1;
        while (from < old_top) {
            Obj *obj = Obj::at(from);
            int size = obj->size();
            if (live.count(from) > 0) {
                if (old_top != top) {
                    Loc to = move_without_forwarding(from, size);
                    forwarding[from] = to;
                }
            }
            else if (old_top == top) {
                top = from;
            }
            from += size;
        }
    }

    static Loc loc_after_move(Loc loc) {
#if COPY_GC
        ForwardingAddress *b = (ForwardingAddress *)Obj::at(loc);
        return (b->type() == Obj::TForward) ? b->to : loc;
#else
        std::map<Loc,Loc>::iterator it = forwarding.find(loc);
        return (it != forwarding.end()) ? it->second : loc;
#endif
    }

    static void fixup_references() {
        ObjRef *p = ObjRef::root;
        while (p) {
            p->loc = loc_after_move(p->loc);
            p = p->next;
        }
#if COPY_GC
        Loc loc = (top >= HeapSemiSize) ? HeapSemiSize : 1;
#else
        Loc loc = 1;
#endif
        while (loc < top) {
            Obj *obj = Obj::at(loc);
            int size = obj->size();
            obj->fixup_references();
            loc += size;
        }
    }

    static void gc() {
#if MARK_SWEEP_GC
        mark_live();
        sweep_garbage();
#else
#if COPY_GC
        move_live();
        fixup_references();
        if (top >= HeapSemiSize) {
            log_free_mem(1, HeapSemiSize - 1);
        }
        else {
            log_free_mem(HeapSemiSize, HeapSemiSize);
        }
#else
#if MARK_COMPACT_GC
        Loc old_top = top;
        compact_live();
        if (old_top > top) {
            fixup_references();
            log_free_mem(top, old_top - top);
        }
#endif
#endif
#endif
    }

    static void add_live_loc(Loc loc) {
        live.insert(loc);
    }

    static void log_roots(std::string msg) {
        ObjRef *p = ObjRef::root;
        std::cout << "['bp','" << msg << "'],\n";
        std::cout << "['roots'";
        live.clear();
        while (p) {
            Loc loc = p->loc;
            std::cout << "," << loc;
            live.insert(loc);
            Obj::at(loc)->traverse(add_live_loc);
            p = p->next;
        }
        std::cout << "],\n";
        std::cout << "['live'";
        std::set<Loc>::iterator it;
        for (it = live.begin(); it != live.end(); ++it) {
            std::cout << "," << *it;
        }
        std::cout << "],\n";
    }

    static char color_of_mem_loc(Loc loc) {
        MemInfo &info = Mem::info[loc];
        if (info.is_allocated) {
            const char *color;
            int age;
            if (info.last_read > info.last_write) {
                color = "0123456789";
                age = info.time - info.last_read;
            }
            else {
                color = "abcdefghij";
                age = info.time - info.last_write;
            }
            if (age == info.time) { return '+'; }
            if (age < 5) {
                return info.is_overhead ? '#' : color[0];
            }
            if (age < 25) { return color[1]; }
            if (age < 125) { return color[2]; }
            return color[3];
        }
        else {
            return ' ';
        }
    }

    // Try to stay under the 2MB spin limit for the resulting animation.

    static void snap() {
        static int frame = 0;
        char xpm_file_name[20];
        sprintf(xpm_file_name, "img%08d.xpm", frame++);

        std::ofstream xpm_file;
        xpm_file.open(xpm_file_name);

        xpm_file << "/* XPM */\n"
                 << "static char * plaid[] =\n"
                 << "{\n"
                 << "/* width height ncolors chars_per_pixel */\n"
                 << "\"" << ImageWidth << " " << ImageHeight << " 11 1\",\n"
                 << "/* colors */\n"
                 << "\"  c black\",\n"
                 << "\"+ c #888888\",\n"
                 << "\"# c #ff0000\",\n"
                 << "\"0 c #00ff00\",\n" // 22ee22
                 << "\"1 c #22cc22\",\n"
                 << "\"2 c #22aa22\",\n"
                 << "\"3 c #228822\",\n"
                 << "\"a c #ffff00\",\n" // eeee22
                 << "\"b c #cccc22\",\n"
                 << "\"c c #aaaa22\",\n"
                 << "\"d c #888822\",\n"
                 << "/* pixels */\n";

        char row[ImageWordSize][ImageWidth + 1];
        for (int py = 0; py < ImageWordSize; ++py) {
            row[py][ImageWidth] = 0;
        }

        int loc_x = 0;
        for (Loc loc = 0; loc < HeapSize; ++loc) {
            char c = color_of_mem_loc(loc);

            for (int py = 0; py < ImageWordSize; ++py) {
                for (int px = 0; px < ImageWordSize; ++px) {
                    row[py][loc_x + px] = c;
                }
            }
#if 0
            if (loc % (frame + 6) == 0) {
                row[1][loc_x + 1] = '#';
            }
#endif
            loc_x += ImageWordSize;

            if (loc_x == ImageWidth) {
                for (int py = 0; py < ImageWordSize; ++py) {
                    xpm_file << "\"" << row[py] << "\",\n";
                }
                loc_x = 0;
            }
        }

        xpm_file << "};\n";
        xpm_file.close();
    }
};

uint MemInfo::time = 0;
UWd Mem::heap[HeapSize];
MemInfo Mem::info[HeapSize];
Loc Mem::top = 0;
std::map<Loc,Loc> Mem::forwarding;
std::set<Loc> Mem::live;

ObjRef *ObjRef::root = 0;
ObjRef *ObjRef::nil = new ObjRef(ObjRef::SHARE, 0);

static bool log_ready = false;
void log_start() { log_ready = true; }
void log_stop() { log_ready = false; }
#define log_msg(M) if (log_ready) { std::cout << M; Mem::snap(); }

void log_alloc_mem(Loc loc, int size) {
    for (int i = 0; i < size; ++i) {
        Mem::info[loc + i].was_allocated();
    }
    log_msg("['alloc'," << loc << ',' << size << "],\n");
}

void log_free_mem(Loc loc, int size) {
    for (int i = 0; i < size; ++i) {
        Mem::info[loc + i].was_freed();
    }
    log_msg("['free'," << loc << ',' << size << "],\n");
}

void log_init_obj(void *addr, const char *type) {
    if (log_ready) {
        std::cout << "['init'," << Mem::addr_to_loc(addr) << ",'" << type << "'],\n";
    }
}

void log_ref_count(Loc loc, int ref_count) {
    Mem::info[loc].was_overhead();
    log_msg("['ref_count'," << loc << "," << ref_count << "],\n");
}

void log_ref_count(void *addr, int ref_count) {
    log_ref_count(Mem::addr_to_loc(addr), ref_count);
}

void log_get_val(const void *addr) {
    Loc loc = Mem::addr_to_loc(addr);
    Mem::info[loc].was_read();
    if (log_ready) {
        Mem::snap();
    }
}

void log_set_val(void *addr, char val) {
    Loc loc = Mem::addr_to_loc(addr);
    Mem::info[loc].was_written();
    log_msg("['set'," << loc << ",\"'" << val << "\"],\n");
}

void log_set_val(void *addr, int val) {
    Loc loc = Mem::addr_to_loc(addr);
    Mem::info[loc].was_written();
    log_msg("['set'," << loc << ",'=" << val << "'],\n");
}

void log_set_ref(void *addr, Loc val) {
    Loc loc = Mem::addr_to_loc(addr);
    Mem::info[loc].was_written();
    log_msg("['set'," << loc << "," << val << "],\n");
}

void log_copy_mem(Loc to, Loc from, int size) {
    for (int i = 0; i < size; ++i) {
        Mem::info[from + i].was_read();
        Mem::info[to + i].was_written();
    }
    log_msg("['copy'," << to << ',' << from << ',' << size << "],\n");
}

void log_copy_mem(void *to, void *from, int size) {
    log_copy_mem(Mem::addr_to_loc(to), Mem::addr_to_loc(from), size);
}

ObjRef::ObjRef(RefType type, UWd loc_or_size, UWd new_size) {
    switch (type) {
        case ALLOC:
            loc = Mem::alloc(loc_or_size);
            referenced_Obj()->init_ref_count();
            break;
        case COPY:
            loc = Mem::copy(loc_or_size, new_size);
            referenced_Obj()->init_ref_count();
            break;
        case SHARE:
            loc = Mem::read_barrier(loc_or_size);
            referenced_Obj()->inc_ref_count();
            break;
    }
    add_to_root_set();
}

ObjRef::ObjRef(const ObjRef &that) {
    loc = Mem::read_barrier(that.loc);
    referenced_Obj()->inc_ref_count();
    add_to_root_set();
}

ObjRef::~ObjRef() {
    if (next) { next->prev = prev; }
    if (prev) { prev->next = next; }
    if (root == this) {
        root = next;
    }
    if (referenced_Obj()->dec_ref_count()) {
        Mem::free(loc, referenced_Obj()->size());
    }
    prev = 0;
    next = 0;
    loc = 0;
}

Loc ObjRef::share() {
    loc = Mem::read_barrier(loc);
    referenced_Obj()->inc_ref_count();
    return loc;
}

void ObjRef::unshare(Loc loc) {
    if (loc) {
        Obj *obj = Obj::at(loc);
        if (obj->dec_ref_count()) {
            Mem::free(loc, obj->size());
        }
    }
}

// Value classes (not part of the GC system)

class Num: public Obj {
  public:
    SWd val;

    void init(SWd _val) {
        Obj::init(TNum);
        val = _val;
        log_set_val(&val, val);
    }

    void set(SWd _val) {
        val = _val;
        log_set_val(&val, val);
    }

    UWd size() const { return size_needed(); }

    SWd to_i() const {
        log_get_val(&val);
        return val;
    }

    void dump() const { std::cout << val; }
    static UWd size_needed() { return sizeof(Num) / sizeof(UWd); }
};

class NumRef: public ObjRef {
  public:
    Num *cast_Num() const { return (Num *)referenced_Obj(); }
    NumRef(SWd val = 0) : ObjRef(ALLOC, Num::size_needed()) {
        cast_Num()->init(val);
    }

    void set(SWd val) { cast_Num()->set(val); }
};

class Tup: public Obj {
  public:
    UWd len;
    Loc val[];

    void init(int _len) {
        Obj::init(TTup);
        len = _len;
        log_set_val(&len, len);
        // due to the shallow copy constructor, there may be initial
        // values in this tuple which need their ref counts bumped.
        for (int i = 0; i < len; ++i) {
            if (val[i]) {
                Obj::at(val[i])->inc_ref_count();
            }
        }
    }

    static Tup *at(Loc loc) { return (Tup *)Obj::at(loc); }

    ObjRef get(int i) const {
        assert(i < len);
        log_get_val(&val[i]);
        return ObjRef::at(val[i]);
    }

    void set(int i, ObjRef obj) {
        assert(i < len);
        // always increment the ref count before decrementing
        // otherwise self-assignment will fail.
        Loc tmp = obj.share();
        ObjRef::unshare(val[i]);
        val[i] = tmp;
        log_set_ref(val + i, val[i]);
    }

    void traverse(VisitFn f) const {
        for (int i = 0; i < len; ++i) {
            log_get_val(&val[i]);
            f(val[i]);
            Obj::at(val[i])->traverse(f);
        }
    }

    void fixup_references() {
        for (int i = 0; i < len; ++i) {
            val[i] = Mem::loc_after_move(val[i]);
        }
    }

    void cleanup() {
        for (int i = 0; i < len; ++i) {
            ObjRef::unshare(val[i]);
            val[i] = 0;
        }
    }

    UWd size() const { return size_needed(len); }
    void dump_up_to(int max) const {
        std::cout << '[';
        for (int i = 0; i < max; ++i) {
            if (i > 0) {
                std::cout << ',';
            }
            Obj::at(val[i])->dump();
        }
        std::cout << ']';
    }
    void dump() const { dump_up_to(len); }
    static UWd size_needed(int len) { return sizeof(Tup) / sizeof(UWd) + len; }
};

class TupRef: public ObjRef {
  public:
    Tup *cast_Tup() const { return (Tup *)referenced_Obj(); }
    TupRef(int len = 2) : ObjRef(ALLOC, Tup::size_needed(len)) {
        cast_Tup()->init(len);
    }
    // FIXME busted if a GC happens during a copy
    TupRef(Loc src, int len) : ObjRef(COPY, src, Tup::size_needed(len)) {
        cast_Tup()->init(len);
    }

    int length() const { return cast_Tup()->len; }
    ObjRef get(int i) const { return cast_Tup()->get(i); }
    void set(int i, ObjRef obj) { cast_Tup()->set(i, obj); }
};

class Vec: public Obj {
  public:
    UWd len;
    Loc tup;

    void init(Loc _tup) {
        Obj::init(TVec);
        len = 0;
        tup = _tup; // caller already incremented ref count
        log_set_val(&len, len);
        log_set_ref(&tup, tup);
    }

    ObjRef get(int i) const {
        assert(i < len);
        log_get_val(&tup);
        return Tup::at(tup)->get(i);
    }

    ObjRef get(int i, int j) const {
        ObjRef inner = get(i);
        assert(inner.type() == TTup || inner.type() == TVec);
        if (inner.type() == TTup) {
            Tup *inner_tup = (Tup *)inner.referenced_Obj();
            return inner_tup->get(j);
        }
        else {
            Vec *inner_vec = (Vec *)inner.referenced_Obj();
            return inner_vec->get(j);
        }
    }

    void set(int i, ObjRef obj) {
        assert(i < len);
        log_get_val(&tup);
        Tup::at(tup)->set(i, obj);
    }

    void traverse(VisitFn f) const {
        log_get_val(&tup);
        f(tup);
        Tup::at(tup)->traverse(f);
    }

    void fixup_references() {
        tup = Mem::loc_after_move(tup);
    }

    void cleanup() {
        ObjRef::unshare(tup);
        tup = 0;
    }

    UWd size() const { return size_needed(len); }
    void dump() const { Tup::at(tup)->dump_up_to(len); }
    static UWd size_needed(int len) { return sizeof(Vec) / sizeof(UWd); }
};

class VecRef: public ObjRef {
  public:
    Vec *cast_Vec() const { return (Vec *)referenced_Obj(); }
    VecRef(int size = 1) : ObjRef(ALLOC, Vec::size_needed(size)) {
        Loc tup = TupRef(size).share();
        cast_Vec()->init(tup);
    }
    VecRef(ObjRef that) : ObjRef(that) {
        assert(that.type() == Obj::TVec);
    }

    int length() const { return cast_Vec()->len; }
    ObjRef get(int i) const { return cast_Vec()->get(i); }
    ObjRef get(int i, int j) const { return cast_Vec()->get(i, j); }
    void set(int i, ObjRef obj) { cast_Vec()->set(i, obj); }

    void push(ObjRef obj) {
        std::cout << "// push "; obj.dump(); std::cout << '\n';
        Vec *vec = cast_Vec();
        Tup *tup = Tup::at(vec->tup);
        if (tup->len == vec->len) {
            Loc new_tup = TupRef(vec->tup, 2 * vec->len).share();
            vec = cast_Vec();
            ObjRef::unshare(vec->tup);
            vec->tup = new_tup;
            tup = Tup::at(vec->tup);
            log_set_ref(&vec->tup, vec->tup);
        }
        tup->set(vec->len, obj);
        vec->len += 1;
        log_set_val(&vec->len, vec->len);
    }

    bool contains(int j, const ObjRef &obj) {
        Vec *vec = cast_Vec();
        for (int i = 0; i < vec->len; ++i) {
            ObjRef other = vec->get(i, j);
            if (obj.equals(other)) {
                return true;
            }
        }
        return false;
    }
};

class Str: public Obj {
  public:
    UWd len;
    UWd val[]; // array of char

    void init(std::string data) {
        Obj::init(TStr);
        len = data.length();
        log_set_val(&len, len);
        for (int i = 0; i < len; ++i) {
            val[i] = data[i];
            log_set_val(val + i, data[i]);
        }
    }

    void init(int _len) {
        Obj::init(TStr);
        len = _len;
        log_set_val(&len, len);
    }

    int split(char sep, int begin[], int end[]) {
        int found = 0;
        int last = 0;
        for (int i = 0; i < len; ++i) {
            log_get_val(&val[i]);
            if (val[i] == sep) {
                begin[found] = last;
                end[found] = i;
                last = i + 1;
                ++found;
            }
        }
        begin[found] = last;
        end[found] = len;
        return found + 1;
    }

    void copy(int begin, int end, Str *dest) {
        for (int i = 0; i < end - begin; ++i) {
            dest->val[i] = val[begin + i];
        }
        log_copy_mem(dest->val, val + begin, end - begin);
    }

    UWd size() {
        return size_needed(len);
    }

    SWd to_i() {
        SWd n = 0;
        int sign = 1;
        int i = 0;
        while (i < len) {
            log_get_val(&val[i]);
            if (val[i] == '-') {
                sign = -sign;
                ++i;
            }
            else {
                break;
            }
        }
        while (i < len) {
            log_get_val(&val[i]);
            if ('0' <= val[i] && val[i] <= '9') {
                n = n * 10 + (val[i] - '0');
                ++i;
            }
            else {
                break;
            }
        }
        return sign * n;
    }

    void dump() {
        std::cout << '"';
        for (int i = 0; i < len; ++i) {
            std::cout << char(val[i]);
        }
        std::cout << '"';
    }

    static UWd size_needed(int len) {
        return sizeof(Str) / sizeof(UWd) + len;
    }

    static UWd size_needed(std::string data) {
        return sizeof(Str) / sizeof(UWd) + data.length();
    }
};

class StrRef: public ObjRef {
  public:
    Str *cast_Str() const { return (Str *)referenced_Obj(); }
    StrRef(std::string data) : ObjRef(ALLOC, Str::size_needed(data)) {
        cast_Str()->init(data);
    }

    StrRef(int len) : ObjRef(ALLOC, Str::size_needed(len)) {
        cast_Str()->init(len);
    }

    VecRef split(char sep) {
        int begin[5];
        int end[5];
        int count = cast_Str()->split(sep, begin, end);
        VecRef fields(count);
        for (int i = 0; i < count; ++i) {
            StrRef substr(end[i] - begin[i]);
            cast_Str()->copy(begin[i], end[i], substr.cast_Str());
            fields.push(substr);
        }
        return fields;
    }
};

// enum Type { TNil=0, TForward=1, TFree=2, TNum=3, TTup=4, TVec=5, TStr=6 };
const char *Obj::TypeName[] = { ":nil ", ":* ", ":- ", ":n ", ":<> ", ":[] ", ":s " };

Obj *Obj::at(Loc loc) {
    return (Obj *)(Mem::heap + loc);
}

void Obj::traverse(VisitFn f) const {
    switch (type()) {
        case TTup:
            return ((Tup *)this)->traverse(f);
        case TVec:
            return ((Vec *)this)->traverse(f);
        default:
            return;
    }
}

void Obj::fixup_references() {
    switch (type()) {
        case TTup:
            return ((Tup *)this)->fixup_references();
        case TVec:
            return ((Vec *)this)->fixup_references();
        default:
            return;
    }
}

void Obj::cleanup() {
    switch (type()) {
        case TTup:
            return ((Tup *)this)->cleanup();
        case TVec:
            return ((Vec *)this)->cleanup();
        default:
            return;
    }
}

UWd Obj::size() const {
    switch (type()) {
        case TNum:
            return ((Num *)this)->size();
        case TTup:
            return ((Tup *)this)->size();
        case TVec:
            return ((Vec *)this)->size();
        case TStr:
            return ((Str *)this)->size();
        case TFree:
            return ((FreeBlock *)this)->size();
        default:
            assert(type() != TForward);
            return 1;
    }
}

SWd Obj::to_i() const {
    switch (type()) {
        case TNum:
            return ((Num *)this)->to_i();
        case TStr:
            return ((Str *)this)->to_i();
        default:
            return 0;
    }
}

bool Obj::equals(const Obj *that) const {
    switch (type()) {
        case TNum:
            if (that->type() == TNum) {
                const Num *a = ((const Num *)this);
                const Num *b = ((const Num *)that);
                return a->val == b->val;
            }
            return false;
        case TStr:
            if (that->type() == TStr) {
                const Str *a = ((const Str *)this);
                const Str *b = ((const Str *)that);
                return a->len == b->len && (a->len == 0 || a->val[0] == b->val[0]);
            }
            return false;
        default:
            return false;
    }
}

void Obj::dump() const {
    switch (type()) {
        case TNil:
            std::cout << "nil";
            break;
        case TNum:
            ((Num *)this)->dump();
            break;
        case TTup:
            ((Tup *)this)->dump();
            break;
        case TVec:
            ((Vec *)this)->dump();
            break;
        case TStr:
            ((Str *)this)->dump();
            break;
        default:
            std::cout << "<Obj? type=" << type() << ">";
            break;
    }
}

/* The C++ main is a close analogue of this Ruby code:

dkp_log = File.foreach("dkp.log").map { |line|
  amount, person, thing = line.strip.split(",")
  [ amount.to_i, person, thing ]
}

standings = dkp_log.group_by { |trans| trans[1] }.map { |person, history|
  [ person, history.reduce(0) { |sum, trans| sum + trans[0] } ]
}.sort { |a, b| b[1] <=> a[1] }

*/

int main(int argc, char **argv) {
    const char *dkp_file_name = (argc == 2 && argv[1]) ? argv[1] : "data/dkp.log-small";

    assert(Num::size_needed() == 2);
    assert(Str::size_needed("hello") == 7);
    assert(Tup::size_needed(5) == 7);
    assert(Vec::size_needed(5) == 3);

    Mem::info[0].was_allocated();
    Mem::top = 1; // heap[0] is nil

    std::cout << "var frame_content = [\n";
    log_start();

    VecRef *dkp_log = new VecRef();
    int bp = 0;

    std::ifstream dkp_file;
    dkp_file.open(dkp_file_name);
    for (std::string data; std::getline(dkp_file, data); ) {
        std::cout << "// line: " << data << '\n';
        StrRef line(data);                 // allocate input line
        VecRef field(line.split(','));     // split into Vec of Str
        TupRef trans(3);                   // allocate 3 tuple
        NumRef amt(field.get(0).to_i());   // convert field 1 to num
        trans.set(0, amt);                 // trans[0] = Num
        trans.set(1, field.get(1));        // trans[1] = Str
        trans.set(2, field.get(2));        // trans[2] = Str
        dkp_log->push(trans);
        if (bp++ == 1) {
            Mem::log_roots("line parsed");
        }
        if (bp % 5 == 0) {
            Mem::gc();
        }
    }
    dkp_file.close();

    Mem::log_roots("file parsed");
    std::cout << "// "; dkp_log->dump(); std::cout << '\n';

    int dkp_log_length = dkp_log->length();
    VecRef *dkp_group = new VecRef();
    bp = 0;

    for (int i = 0; i < dkp_log_length; ++i) {
        if (!dkp_group->contains(0, dkp_log->get(i, 1))) {
            TupRef person(2);
            person.set(0, dkp_log->get(i, 1));
            VecRef history;
            person.set(1, history);
            dkp_group->push(person);
            for (int j = i; j < dkp_log_length; ++j) {
                if (dkp_log->get(j, 1).equals(person.get(0))) {
                    history.push(dkp_log->get(j));
                }
            }
            if (bp++ == 1) {
                Mem::log_roots("group found");
            }
        }
    }

    delete dkp_log;
    dkp_log = 0;

    Mem::gc();

    Mem::log_roots("data grouped");
    std::cout << "// "; dkp_group->dump(); std::cout << '\n';
    bp = 0;

    int dkp_group_length = dkp_group->length();
    VecRef *dkp_standing = new VecRef();

    for (int i = 0; i < dkp_group_length; ++i) {
        TupRef person(2);
        person.set(0, dkp_group->get(i, 0));
        VecRef history = dkp_group->get(i, 1);
        int sum = 0;
        NumRef final(sum);
        for (int j = 0; j < history.length(); ++j) {
            NumRef tmp(sum + history.get(j, 0).to_i());
            sum = tmp.to_i();
        }
        final.set(sum);
        person.set(1, final);
        dkp_standing->push(person);
        if (bp++ == 1) {
            Mem::log_roots("transaction history reduced");
        }
    }

    delete dkp_group;
    dkp_group = 0;

    Mem::gc();

    int dkp_standing_length = dkp_standing->length();
    VecRef *dkp_rank = new VecRef(dkp_standing_length);

    // world's most terrible sort
    for (int rank = 20; rank >= 0; --rank) {
        for (int i = 0; i < dkp_standing_length; ++i) {
            if (dkp_standing->get(i, 1).to_i() == rank) {
                dkp_rank->push(dkp_standing->get(i));
            }
        }
    }

    delete dkp_standing;
    dkp_standing = 0;

    Mem::gc();

    Mem::log_roots("ranking finished");
    std::cout << "// "; dkp_rank->dump(); std::cout << '\n';
    log_stop();
    std::cout << "['stop']];\n";

    delete dkp_rank;
    dkp_rank = 0;

    return 0;
}
