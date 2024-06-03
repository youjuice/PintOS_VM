/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "userprog/syscall.h"
#include "threads/mmu.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *page = malloc(sizeof(struct page));
		if (page == NULL)
			goto err;

		page->writable = writable;

		switch (type) {
			case VM_ANON:
				uninit_new(page, upage, init, type, aux, anon_initializer);
				break;
			case VM_FILE:
				uninit_new(page, upage, init, type, aux, file_backed_initializer);
				break;
			default:
				break;
		}

		/* TODO: Insert the page into the spt. */
		if (!spt_insert_page(spt, page)) {
			free(page);
			goto err;
		}
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *search_page = malloc(sizeof(struct page));		// 검색용 page 할당
	void *temp_va = pg_round_down(va);
	search_page->va = temp_va;

	struct hash_elem *find_elem = hash_find(&spt->pages, &search_page->h_elem);
	free(search_page);											// 임시 page 메모리 해제

	if (find_elem == NULL)		return NULL;
	return hash_entry(find_elem, struct page, h_elem);
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	return hash_insert(&spt->pages, &page->h_elem) == NULL;
}

bool
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	hash_delete(&spt->pages, &page->h_elem);
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = malloc(sizeof(struct frame));
	/* TODO: Fill this function. */
	void* kva = palloc_get_page(PAL_ZERO | PAL_USER);
	if (kva != NULL) {
		frame->kva = kva;
		frame->page = NULL;
	}
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL)		return false;

	/* TODO: Fill this function */
	if (pml4_get_page(&thread_current()->pml4, va) == NULL)
		return vm_do_claim_page (page);

	return false;
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if(pml4_set_page(&thread_current()->pml4, page->va, frame->kva, page->writable))
		return swap_in (page, frame->kva);
	else
		return false;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&thread_current()->spt.pages, vm_hash_func, vm_less_func, NULL);

	// 초기화 실패하면 -> 프로그램 종료 해야할까??
	if (!hash_init(&thread_current()->spt.pages, vm_hash_func, vm_less_func, NULL)) 	
		exit(-1);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}


/* ========== Custom Function ========== */
unsigned
vm_hash_func (const struct hash_elem *e, void *aux) {
	const struct page *page_e = hash_entry(e, struct page, h_elem);
	return hash_int((uintptr_t)page_e->va);
}

bool
vm_less_func (const struct hash_elem *a, const struct hash_elem *b) {
	const struct page *page_a = hash_entry(a, struct page, h_elem);
	const struct page *page_b = hash_entry(b, struct page, h_elem);

	return page_a->va < page_b->va;
}

// For syscall.c
void
check_valid_buffer (void *buffer, unsigned size, void *rsp, bool to_write) {
	uint8_t *buf_addr = (uint8_t *)buffer;
	uint8_t *end_addr = buf_addr + size - 1;

	// Case 1. buffer의 크기가 한 페이지를 넘지 않는 경우
	if (pg_round_down(buf_addr) == pg_round_down(end_addr)) {
		struct page *page = check_address(buffer, rsp);
		if (page == NULL || page->writable != to_write) 
			exit(-1);
	}
	// Case 2. buffer의 크기가 한 페이지를 넘는 경우
	else {
		for (uint8_t *addr = buf_addr; addr <= end_addr; addr += PGSIZE) {
			struct page *page = check_address(addr, rsp);
			if (page == NULL || page->writable != to_write) 
				exit(-1);
		}
	}
}
