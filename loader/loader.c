#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "exec_parser.h"

int ceiling(int x, int y)
{
	if (!x % y)
		return x / y;
	else
		return (x / y) + 1;
}

static so_exec_t *exec;
static struct sigaction default_handler;
static int exec_fd;

int get_segment_number_for_address(uintptr_t address)
{
	// parcurg toate segmentele executabilului
	for (int i = 0; i < exec->segments_no; i++)
	{
		// adresa de start a segmenului i
		uintptr_t base_address = exec->segments[i].vaddr;
		unsigned int seg_size = exec->segments[i].mem_size;
		// verific daca adresa se afla in zona de memorie pentru segmentul i
		if (address >= base_address && address <= (base_address + (uintptr_t)seg_size))
		{
			return i;
		}
	}
	return -1;
}

so_seg_t *get_segment(int seg_num)
{
	if (seg_num < 0 || seg_num >= exec->segments_no)
		return NULL;
	return &exec->segments[seg_num];
}

int get_page_index(uintptr_t segment_base_address, uintptr_t address, int page_size)
{
	int segment_offset = (uintptr_t)(address - segment_base_address);
	return segment_offset / page_size;
}

void mmap_page(so_seg_t* target_segment,int page_no,int page_size,int mapp_size,int flags)
{
	void* mmap_result = mmap((void *)target_segment->vaddr + page_no * page_size, mapp_size, target_segment->perm,
		flags, exec_fd, target_segment->offset + page_no * page_size);
	if (mmap_result == MAP_FAILED)
	{
		printf("Eroare la mapare pagina\n");
		exit(EXIT_FAILURE);
	}
}

static void segv_handler(int signum, siginfo_t *info, void *context)
{
	// obtine adresa care a generat page fault-ul
	uintptr_t address = (uintptr_t)info->si_addr;

	// Verificam din ce segment face parte adresa care a generat page fault-ul
	int seg_num = get_segment_number_for_address(address);
	if (seg_num == -1)
	{
		// page fault-ul nu se gaseste intr-un segment.
		// a avut loc acces invalid la memorie. Apeleaza handler-ul implicit
		default_handler.sa_sigaction(signum, info, context);
		return;
	}
	// Determinam pagina corespunzatoare pentru segmentul dat
	so_seg_t *target_segment = get_segment(seg_num);
	int page_size = getpagesize();
	int page_no = get_page_index(target_segment->vaddr, address, page_size);
	// Verificam daca pagina a fost deja mapata
	int *page_mapping_vect = (int *)target_segment->data;
	if (page_mapping_vect[page_no] != 0)
	{
		// Pagina e deja mapata => acces nepermis la memorie => se ruleaza handler-ul implicit
		default_handler.sa_sigaction(signum, info, context);
	}
	// Pagina se gaseste intr-un segment si nu a fost mapata.
	// Mapam pagina corespunzatoare din executabil
	// Avem mai multe cazuri posibile

	// Cazul 1: dimensiunea segmentului in memorie = dimensiunea segmentului din fisier
	if (target_segment->file_size == target_segment->mem_size)
	{
		// Subcazul1: avem o pagina intreaga de mapat
		if (target_segment->file_size >= (page_no + 1) * page_size)
		{
			// Mapam o pagina intreaga
			mmap_page(target_segment,page_no,page_size,page_size,MAP_PRIVATE|MAP_FIXED);
		}
		else
		{
			// Subcazul2: Mapam ultima pagina
			int last_bytes = target_segment->file_size - page_size * page_no;
			// Daca last_bytes nu e multiplu de pagina => ce ramane va fi automat completat cu zero (asa zice in mmap(2)).
			mmap_page(target_segment,page_no,page_size,last_bytes,MAP_PRIVATE|MAP_FIXED);
		}
	}
	// Cazul2: dimensiunea segmentului din memorie > dimensiunea segmentului din fisier
	if (target_segment->file_size < target_segment->mem_size)
	{
		// Subcazul1: avem o pagina intreaga de mapat
		if (target_segment->file_size >= (page_no + 1) * page_size)
		{
			// Mapam o pagina intreaga
			mmap_page(target_segment,page_no,page_size,page_size,MAP_PRIVATE|MAP_FIXED);
		}
		else
		{
			// Subcazul2: avem o pagina partiala de mapat(ultima pagina din executabil)
			if ((target_segment->file_size < (page_no + 1) * page_size) && (target_segment->file_size > page_no * page_size))
			{
				int last_bytes = target_segment->file_size - page_size * page_no;
				mmap_page(target_segment,page_no,page_size,last_bytes,MAP_PRIVATE|MAP_FIXED);

				// Restul de bytes pana la final ii umplem cu 0
				memset((void *)target_segment->vaddr + target_segment->file_size, 0, (page_no + 1) * page_size - target_segment->file_size);
			}
			else
			{
				// Subcazul3: avem o pagina ce a trecut de file_size si trebuie zeroizata pana la mem_size
				// Mapam toata pagina cu 0
				mmap_page(target_segment,page_no,page_size,page_size,MAP_PRIVATE | MAP_FIXED | MAP_ANON);
			}
		}
	}
	page_mapping_vect[page_no] = 1;
}

int so_init_loader(void)
{
	int rc;
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));

	rc = sigemptyset(&sa.sa_mask);
	if (rc < 0)
		return -1;
	rc = sigaddset(&sa.sa_mask, SIGSEGV);
	if (rc < 0)
		return -1;
	
	sa.sa_sigaction = segv_handler;
	sa.sa_flags = SA_SIGINFO;
	rc = sigaction(SIGSEGV, &sa, &default_handler);
	if (rc < 0)
	{
		perror("sigaction");
		return -1;
	}
	return 0;
}

void init_so_exec_data(so_exec_t *exec)
{
	// Campul data din structura so_exec_t retine
	// un vector de int-uri pentru fiecare segment
	// ce ne spune ce pagini au fost si ce pagini nu
	// au fost mapate
	int page_size = getpagesize();
	for (int i = 0; i < exec->segments_no; i++)
	{
		int int_pages_number = ceiling(exec->segments[i].mem_size, page_size);

		// aloca vector in structura data a lui exec
		int *pages_mapping = calloc(int_pages_number, sizeof(int));
		if (pages_mapping == NULL)
		{
			printf("Eroare la alocare data pt pagini\n");
			exit(EXIT_FAILURE);
		}
		exec->segments[i].data = (void *)pages_mapping;
	}
}

int so_execute(char *path, char *argv[])
{
	exec = so_parse_exec(path);
	if (!exec)
		return -1;

	// Deschidem file descriptorul pentru fisierul executabil
	exec_fd = open(path, O_RDONLY);
	if (exec_fd < 0)
		return -1;

	init_so_exec_data(exec);

	so_start_exec(exec, argv);

	for (int i = 0; i < exec->segments_no; i++)
		free(exec->segments[i].data);

	close(exec_fd);

	return -1;
}