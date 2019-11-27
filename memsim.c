//
// Virual Memory Simulator Homework
// One-level page table system with FIFO and LRU
// Two-level page table system with LRU
// Inverted page table with a hashing system 
// Submission Year: 2019
// Student Name: 김우경 
// Student Number: B711035
//
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define PAGESIZEBITS 12			// page size = 4Kbytes
#define VIRTUALADDRBITS 32		// virtual address space size = 4Gbytes
#define NUMBEROFVPN 1048576


//페이지 테이블: 레벨은 1, 각 entry 마다 valid bit, 해당 가상주소가 사상된 PFN 
struct pageTableEntry {
	int level;				// page table level (1 or 2)
	char valid;
	struct pageTableEntry *secondLevelPageTable;	// valid if this entry is for the first level page table (level = 1)
	int frameNumber;								// valid if this entry is for the second level page table (level = 2)
};

//프레임 페이지의 링크드리스트: 프레임 넘버, 해당 프레임 갖고있는 pid, 사상된 virtual page
struct framePage {
	int number;			// frame number
	int pid;			// Process id that owns the frame
	int virtualPageNumberforFirst;			// virtual page number using the frame
	int virtualPageNumberforSecond;
	struct framePage *lruLeft;	// for LRU circular doubly linked list
	struct framePage *lruRight; // for LRU circular doubly linked list
};

struct invertedPageTableEntry {
	int pid;					// process id
	int virtualPageNumber;		// virtual page number
	int frameNumber;			// frame number allocated
	struct invertedPageTableEntry *next;
};

//프로세스 테이블: 각 프로세스마다 trace 파일 이름, 처리한 memory trace 수, page fault 수, page hit 수 등의 정보
struct procEntry {
	char *traceName;			// the memory trace name
	int pid;					// process (trace) id
	int ntraces;				// the number of memory traces
	int num2ndLevelPageTable;	// The 2nd level page created(allocated);
	int numIHTConflictAccess; 	// The number of Inverted Hash Table Conflict Accesses
	int numIHTNULLAccess;		// The number of Empty Inverted Hash Table Accesses
	int numIHTNonNULLAcess;		// The number of Non Empty Inverted Hash Table Accesses
	int numPageFault;			// The number of page faults
	int numPageHit;				// The number of page hits
	struct pageTableEntry *firstLevelPageTable;
	FILE *tracefp;
};

struct framePage *oldestFrame; // the oldest frame pointer -> LRU 에서 사용
int firstLevelBits, phyMemSizeBits, numProcess;
int s_flag = 0;

void initPhyMem(struct framePage *phyMem, int nFrame) {		//프레임 페이지 초기화하기-> 링크드 리스트 쭉 연결
	int i;
	for(i = 0; i < nFrame; i++) {
		phyMem[i].number = i;
		phyMem[i].pid = -1;	//아직 사용하지 x
		phyMem[i].virtualPageNumberforFirst = -1;		//아직 사용하지 x
		phyMem[i].virtualPageNumberforSecond = -1;
		phyMem[i].lruLeft = &phyMem[(i-1+nFrame) % nFrame];
		phyMem[i].lruRight = &phyMem[(i+1+nFrame) % nFrame];
	}
	oldestFrame = &phyMem[0];
}

void initProcTab(struct procEntry *procT, int nProc){
	int i;
	for(i = 0; i < numProcess; i++) {
		// initialize proc fields
		procT[i].pid = i;
		procT[i].ntraces = 0;
		procT[i].num2ndLevelPageTable = 0;
		procT[i].numIHTConflictAccess = 0;
		procT[i].numIHTNULLAccess = 0;
		procT[i].numIHTNonNULLAcess = 0;
		procT[i].numPageFault = 0;
		procT[i].numPageHit = 0;
		procT[i].firstLevelPageTable = NULL;	
		procT[i].tracefp = fopen(procT[i].traceName, "r");
	}
}

//
void initPageTableforOne(struct pageTableEntry* pageT, int firstLevelEntries){
	int i, count=0;

	for(i=0; i<firstLevelEntries; i++){
		pageT[i].level = 1;				// page table level (1 or 2)
		pageT[i].valid = 'N';	//아무것도 사상되지 않은 초기는 invalid
		pageT[i].secondLevelPageTable = NULL;	// valid if this entry is for the first level page table (level = 1)
		pageT[i].frameNumber = -1; //아무 프레임도 할당되지 않았음을 의미
		count++;
	}

}

void initPageTableforTwo(struct pageTableEntry* pageT, int secondLevelEntries ){
	int i;

	for(i=0 ; i<secondLevelEntries ; i++){
		pageT[i].level = 2;
		pageT[i].valid = 'N';
		pageT[i].secondLevelPageTable = NULL;
		pageT[i].frameNumber = -1;
	}
}

void oneLevelVMSim(struct procEntry *procTable, struct framePage *phyMemFrames, char mode) {
	unsigned Vaddr, Paddr, offset, VPN, PFN;
	char rw;
	int k;
	int count = 0;
	
	//PageTable은 프로세스마다 할당 -> entry 2^20개 -> 초기화 위치가???,,,
	// struct pageTableEntry **pageTable = (struct pageTableEntry**)malloc(sizeof(struct pageTableEntry*)*numProcess);		//이차원 배열 동적할당 ?????
	// printf("PT process 별로 할당 완료 \n");
	for(k=0; k<numProcess; k++){
		procTable[k].firstLevelPageTable = (struct pageTableEntry*)malloc(sizeof(struct pageTableEntry)*NUMBEROFVPN);
		initPageTableforOne(procTable[k].firstLevelPageTable, NUMBEROFVPN);
		// printf("PTE 2^20개로 할당 완료 \n");
	}

	
	while(1)
	{
		int i = 0;
		for(i = 0; i < numProcess; i++)
		{
			if(fscanf(procTable[i].tracefp, "%x %c", &Vaddr, &rw) == EOF){		//tracefile을 끝까지 읽은 프로세스 갯수 세기
				count++;
				continue;
			}
			
			else{
				offset = Vaddr & 0xfff;
				VPN = Vaddr >> 12;
				// printf("offset: %x\n",offset);
				// printf("VPN: %x\n",VPN);
				if(mode == 'F'){
					if( procTable[i].firstLevelPageTable[VPN].valid == 'Y'){		//i번째 프로세스의 varr를 인덱스로 접근하기
						PFN = procTable[i].firstLevelPageTable[VPN].frameNumber;
						if(phyMemFrames[PFN].pid == i && phyMemFrames[PFN].virtualPageNumberforFirst == VPN){
							// hit 조건 충족
							procTable[i].numPageHit++;
							procTable[i].ntraces++;
						}
						else{
							int exVPN = oldestFrame->virtualPageNumberforFirst;
							if(exVPN != -1)
								procTable[oldestFrame->pid].firstLevelPageTable[exVPN].valid = 'N';		//새로 사상시키려는 프레임과 이전에 사상되었던 vpn의 pte정보 갱신 -> 더이상 valid 하지 x
							procTable[i].firstLevelPageTable[VPN].frameNumber = oldestFrame->number; //oldestframe이 가리키는 frame과 사상
							procTable[i].firstLevelPageTable[VPN].valid = 'Y';		//PTE정보 update
							oldestFrame->pid = i;		//frame의 정보 update
							oldestFrame->virtualPageNumberforFirst = VPN;
							oldestFrame = oldestFrame->lruRight;		//oldestframe이 다음꺼 가리키게
							
							PFN = procTable[i].firstLevelPageTable[VPN].frameNumber;
							procTable[i].numPageFault++;
							procTable[i].ntraces++;
						}
					}
					
					else{		//pagefault
						int exVPN = oldestFrame->virtualPageNumberforFirst;
						if(exVPN != -1)
							procTable[oldestFrame->pid].firstLevelPageTable[exVPN].valid = 'N';		//새로 사상시키려는 프레임과 이전에 사상되었던 vpn의 pte정보 갱신 -> 더이상 valid 하지 x
						procTable[i].firstLevelPageTable[VPN].frameNumber = oldestFrame->number; //oldestframe이 가리키는 frame과 사상
						procTable[i].firstLevelPageTable[VPN].valid = 'Y';		//PTE정보 update
						oldestFrame->pid = i;		//frame의 정보 update
						oldestFrame->virtualPageNumberforFirst = VPN;
						oldestFrame = oldestFrame->lruRight;		//oldestframe이 다음꺼 가리키게
						
						PFN = procTable[i].firstLevelPageTable[VPN].frameNumber;
						// printf("PFN: %x\n",PFN);
						procTable[i].numPageFault++;
						procTable[i].ntraces++;
					}
					Paddr = (PFN << 12) + offset;
				}
				
				else if(mode == 'L'){
					if(procTable[i].firstLevelPageTable[VPN].valid == 'Y'){
						PFN = procTable[i].firstLevelPageTable[VPN].frameNumber;
						if(phyMemFrames[PFN].pid == i && phyMemFrames[PFN].virtualPageNumberforFirst == VPN){			//oldest의 왼쪽이 가장 최근에 접근한 frame*****
							//hit 조건 충족

							if(oldestFrame == &phyMemFrames[PFN])
								oldestFrame = oldestFrame->lruRight;

							else{
								phyMemFrames[PFN].lruLeft->lruRight = phyMemFrames[PFN].lruRight;
								phyMemFrames[PFN].lruRight->lruLeft = phyMemFrames[PFN].lruLeft;
								phyMemFrames[PFN].lruRight = oldestFrame;
								phyMemFrames[PFN].lruLeft = oldestFrame->lruLeft;
								oldestFrame->lruLeft->lruRight = &phyMemFrames[PFN];
								oldestFrame->lruLeft = &phyMemFrames[PFN];   
							}

							procTable[i].numPageHit++;
							procTable[i].ntraces++;
						}
						else{
							int exVPN = oldestFrame->virtualPageNumberforFirst;
							if(exVPN!=-1)
								procTable[oldestFrame->pid].firstLevelPageTable[exVPN].valid = 'N';		//새로 사상시키려는 프레임과 이전에 사상되었던 vpn의 pte정보 갱신 -> 더이상 valid 하지 x
							procTable[i].firstLevelPageTable[VPN].frameNumber = oldestFrame->number; //oldestframe이 가리키는 frame과 사상
							procTable[i].firstLevelPageTable[VPN].valid = 'Y';		//PTE정보 update
							oldestFrame->pid = i;		//frame의 정보 update
							oldestFrame->virtualPageNumberforFirst = VPN;
							oldestFrame = oldestFrame->lruRight;		//oldestframe이 다음꺼 가리키게
							
							PFN = procTable[i].firstLevelPageTable[VPN].frameNumber;
							// printf("PFN: %x\n",PFN);
							procTable[i].numPageFault++;
							procTable[i].ntraces++;
						}
					}
					else{		//pagefault시
					//oldest가 가리키는 frame할당하고 oldest<-oldest.lruright
						int exVPN = oldestFrame->virtualPageNumberforFirst;
						if(exVPN!= -1)
							procTable[oldestFrame->pid].firstLevelPageTable[exVPN].valid = 'N';		//새로 사상시키려는 프레임과 이전에 사상되었던 vpn의 pte정보 갱신 -> 더이상 valid 하지 x
						procTable[i].firstLevelPageTable[VPN].frameNumber = oldestFrame->number; //oldestframe이 가리키는 frame과 사상
						procTable[i].firstLevelPageTable[VPN].valid = 'Y';		//PTE정보 update
						oldestFrame->pid = i;		//frame의 정보 update
						oldestFrame->virtualPageNumberforFirst = VPN;
						oldestFrame = oldestFrame->lruRight;		//oldestframe이 다음꺼 가리키게
						
						PFN = procTable[i].firstLevelPageTable[VPN].frameNumber;
						procTable[i].numPageFault++;
						procTable[i].ntraces++;
					}
					Paddr = (PFN << 12) + offset;
				}
				else{
					printf("invalid input");
					exit(1);
				}
				
			}
			if (s_flag)		//-s option print statement
				printf("One-Level procID %d traceNumber %d virtual addr %x physical addr %x\n", i, procTable[i].ntraces,Vaddr,Paddr );
		}
		
		if (count == numProcess)
			break;
	}
	
	for(k=0; k < numProcess; k++) {
		printf("**** %s *****\n",procTable[k].traceName);
		printf("Proc %d Num of traces %d\n",k,procTable[k].ntraces);
		printf("Proc %d Num of Page Faults %d\n",k,procTable[k].numPageFault);
		printf("Proc %d Num of Page Hit %d\n",k,procTable[k].numPageHit);
		assert(procTable[k].numPageHit + procTable[k].numPageFault == procTable[k].ntraces);
	}

	for(k=0; k<numProcess; k++){
		free(procTable[k].firstLevelPageTable);
	}
}


void twoLevelVMSim(struct procEntry *procTable, int firstLevelBits, struct framePage *phyMemFrames) {
	unsigned Vaddr, Paddr, offset, MPN, SPN, PFN;
	char rw;
	int k;
	int count = 0;
	int secondLevelBits = 32 - PAGESIZEBITS - firstLevelBits;
	int firstLevelEntries = 1 << firstLevelBits;
	int secondLevelEntries = 1 << secondLevelBits;


	int count_hit=0, count_fault1=0, count_fault2=0, count_fault3=0;

	// 프로세스 개수만큼 포문돌면서 procTable의 포인터에 firstLevelBits 만큼 동적할당 -> 초기화 해주기
	for(k=0 ; k<numProcess ; k++){
		procTable[k].firstLevelPageTable = (struct pageTableEntry*)malloc(sizeof(struct pageTableEntry)*firstLevelEntries);
		initPageTableforOne(procTable[k].firstLevelPageTable, firstLevelEntries);
	}

	while(1){
		int i = 0;
		int j=0;

		for(i = 0; i < numProcess; i++)
		{
			if(fscanf(procTable[i].tracefp, "%x %c\n", &Vaddr, &rw) == EOF){		//tracefile을 끝까지 읽은 프로세스 갯수 세기
				count++;
				continue;
			}
			else{
				// printf("Vaddr: %x, rw: %c\n", Vaddr, rw);
				//가상주소 - master/secondary/offset 으로 나누기
				offset = Vaddr & 0xfff;
				MPN = Vaddr >> (32 - firstLevelBits); //firstLevelBit=8bit 일때 secondary=12
				SPN = Vaddr << firstLevelBits;
				SPN = SPN >> (12 + firstLevelBits);


				if(procTable[i].firstLevelPageTable[MPN].valid == 'Y'){
					if(procTable[i].firstLevelPageTable[MPN].secondLevelPageTable[SPN].valid == 'Y'){
						PFN = procTable[i].firstLevelPageTable[MPN].secondLevelPageTable[SPN].frameNumber;
						// printf("\n\n\n%x %x %x\n\n\n\n",PFN,phyMemFrames[PFN].virtualPageNumber,SPN);
						if(phyMemFrames[PFN].pid == i && phyMemFrames[PFN].virtualPageNumberforFirst ==MPN && phyMemFrames[PFN].virtualPageNumberforSecond == SPN)
						{
						// //hit조건 만족
						// //lru로 접근된거 제일 앞으로 옮기기
						// printf("*****hit*****\n");

							if(oldestFrame == &phyMemFrames[PFN])
								oldestFrame = oldestFrame->lruRight;

							else{
								phyMemFrames[PFN].lruLeft->lruRight = phyMemFrames[PFN].lruRight;
								phyMemFrames[PFN].lruRight->lruLeft = phyMemFrames[PFN].lruLeft;
								phyMemFrames[PFN].lruRight = oldestFrame;
								phyMemFrames[PFN].lruLeft = oldestFrame->lruLeft;
								oldestFrame->lruLeft->lruRight = &phyMemFrames[PFN];
								oldestFrame->lruLeft = &phyMemFrames[PFN];   
							}

							procTable[i].numPageHit++;
							procTable[i].ntraces++;
							count_hit++;
						}
						
						else{		//pid가 내가 아님 -> process 하나만 돌렸을때 나면 안되는 요륜데
							// printf("*****fault1*****\n");
							int exMPN = oldestFrame->virtualPageNumberforFirst;
							int exSPN = oldestFrame->virtualPageNumberforSecond;
							if(exMPN!=-1 && exSPN != -1)
								if(procTable[oldestFrame->pid].firstLevelPageTable[exMPN].valid != 'N')
								procTable[oldestFrame->pid].firstLevelPageTable[exMPN].secondLevelPageTable[exSPN].valid = 'N';

							procTable[i].firstLevelPageTable[MPN].secondLevelPageTable[SPN].frameNumber = oldestFrame->number; //oldestframe이 가리키는 frame과 사상
							procTable[i].firstLevelPageTable[MPN].secondLevelPageTable[SPN].valid = 'Y';		//PTE정보 update
							oldestFrame->pid = i;		//frame의 정보 update
							oldestFrame->virtualPageNumberforFirst = MPN;
							oldestFrame->virtualPageNumberforSecond = SPN;
							oldestFrame = oldestFrame->lruRight;		//oldestframe이 다음꺼 가리키게
							
							PFN = procTable[i].firstLevelPageTable[MPN].secondLevelPageTable[SPN].frameNumber;
							procTable[i].numPageFault++;
							procTable[i].ntraces++;
							count_fault1++;
						}
					}
					else{ //연결은 됐는데 secondlevel에서 invalid할떄 -> page fault
					//lru이용해서 oldest가 가리키는 넘으로 배정
						// printf("*****fault2*****\n");
						int exMPN = oldestFrame->virtualPageNumberforFirst;
						int exSPN = oldestFrame->virtualPageNumberforSecond;

						if(exMPN != -1 && exSPN != -1)
							if(procTable[oldestFrame->pid].firstLevelPageTable[exMPN].valid != 'N')
								procTable[oldestFrame->pid].firstLevelPageTable[exMPN].secondLevelPageTable[exSPN].valid = 'N';
						procTable[i].firstLevelPageTable[MPN].secondLevelPageTable[SPN].frameNumber = oldestFrame->number; //oldestframe이 가리키는 frame과 사상
						procTable[i].firstLevelPageTable[MPN].secondLevelPageTable[SPN].valid = 'Y';		//PTE정보 update
						oldestFrame->pid = i;		//frame의 정보 update
						oldestFrame->virtualPageNumberforFirst = MPN;
						oldestFrame->virtualPageNumberforSecond = SPN;
						oldestFrame = oldestFrame->lruRight;		//oldestframe이 다음꺼 가리키게
						PFN = procTable[i].firstLevelPageTable[MPN].secondLevelPageTable[SPN].frameNumber;
						procTable[i].numPageFault++;
						procTable[i].ntraces++;
						count_fault2++;
					}
				}
				else{	//아예 firstlevelpage가 secondlevel이랑 연결이 안됐을때	-> page fault
				// printf("*****fault3*****\n");
					procTable[i].firstLevelPageTable[MPN].secondLevelPageTable = (struct pageTableEntry*)malloc(sizeof(struct pageTableEntry)*secondLevelEntries);
					initPageTableforTwo(procTable[i].firstLevelPageTable[MPN].secondLevelPageTable, secondLevelEntries);

					procTable[i].firstLevelPageTable[MPN].secondLevelPageTable[SPN].frameNumber = oldestFrame->number; //oldestframe이 가리키는 frame과 사상
					procTable[i].firstLevelPageTable[MPN].secondLevelPageTable[SPN].valid = 'Y';		//PTE정보 update
					procTable[i].firstLevelPageTable[MPN].valid = 'Y';
					oldestFrame->pid = i;		//frame의 정보 update
					oldestFrame->virtualPageNumberforFirst = MPN;
					oldestFrame->virtualPageNumberforSecond = SPN;
					oldestFrame = oldestFrame->lruRight;		//oldestframe이 다음꺼 가리키게
					
					PFN = procTable[i].firstLevelPageTable[MPN].secondLevelPageTable[SPN].frameNumber;
					procTable[i].numPageFault++;
					procTable[i].ntraces++;
					procTable[i].num2ndLevelPageTable++;
					count_fault3++;
				}
				Paddr = (PFN << 12) + offset; 
			}
			if (s_flag)	//-s option print statement
				printf("Two-Level procID %d traceNumber %d virtual addr %x physical addr %x\n", i, procTable[i].ntraces,Vaddr,Paddr);
		}
		if (count == numProcess)
			break;	
	}
	
	int i;
	for(i=0; i < numProcess; i++) {
		// printf("%d %d %d %d\n", count_hit, count_fault1, count_fault2, count_fault3);

		printf("**** %s *****\n",procTable[i].traceName);
		printf("Proc %d Num of traces %d\n",i,procTable[i].ntraces);
		printf("Proc %d Num of second level page tables allocated %d\n",i,procTable[i].num2ndLevelPageTable);
		printf("Proc %d Num of Page Faults %d\n",i,procTable[i].numPageFault);
		printf("Proc %d Num of Page Hit %d\n",i,procTable[i].numPageHit);
		assert(procTable[i].numPageHit + procTable[i].numPageFault == procTable[i].ntraces);
	}

		for(k=0; k<numProcess; k++){
			free(procTable[k].firstLevelPageTable);
	}
}


void invertedPageVMSim(struct procEntry *procTable, struct framePage *phyMemFrames, int nFrame) {
	//frame개수만큼 iht 할당 

	unsigned Paddr, Vaddr, VPN, PFN, offset, HASH, dHASH;
	char rw;
	int count=0;
	int k;

	struct invertedPageTableEntry** invertedPageTable = (struct invertedPageTableEntry**)malloc(sizeof(struct invertedPageTableEntry*)*nFrame);
	
	for(k=0 ; k<nFrame ; k++){
		invertedPageTable[k] = NULL;
	}

	while(1){
		int i = 0;
		for(i = 0; i < numProcess; i++)
		{
			if(fscanf(procTable[i].tracefp, "%x %c", &Vaddr, &rw) == EOF){		//tracefile을 끝까지 읽은 프로세스 갯수 세기
				count++;
				continue;
			}

			else{
				offset = Vaddr & 0xfff;
				VPN = Vaddr >> 12;
				HASH = (VPN + i) % nFrame;
				procTable[i].ntraces++;

				struct invertedPageTableEntry* current = invertedPageTable[HASH];
				struct invertedPageTableEntry* previous = NULL;

				if(current == NULL){
					//page fault -> hash table is empty
					//new node할당하고 초기화, 올드프레임이 이전에 사상됐으면 지우기
					// printf("*****fault1*****\n");
					struct invertedPageTableEntry *newNode = (struct invertedPageTableEntry*)malloc(sizeof(struct invertedPageTableEntry));
					newNode->pid = i;
					newNode->virtualPageNumber = VPN;
					newNode->frameNumber = oldestFrame->number;
					newNode->next = NULL;
					invertedPageTable[HASH] = newNode;

					if(oldestFrame->virtualPageNumberforFirst != -1){
						dHASH = (oldestFrame->virtualPageNumberforFirst + oldestFrame->pid)%nFrame;
						struct invertedPageTableEntry * dcurrent = invertedPageTable[dHASH];
						struct invertedPageTableEntry * pre_dcurrent = NULL;

						while(dcurrent != NULL){
							if(dcurrent->virtualPageNumber == oldestFrame->virtualPageNumberforFirst && dcurrent->pid == oldestFrame->pid && dcurrent->frameNumber == oldestFrame->number){
								if(pre_dcurrent == NULL) invertedPageTable[dHASH] = dcurrent->next;
								else pre_dcurrent->next = dcurrent->next;
								free(dcurrent);
								break;
							}
							pre_dcurrent = dcurrent;
							dcurrent = dcurrent->next;
						}
					}

					PFN = newNode->frameNumber;
					oldestFrame->pid = i;
					oldestFrame->virtualPageNumberforFirst = VPN;
					oldestFrame = oldestFrame->lruRight;

					procTable[i].numPageFault++;
					procTable[i].numIHTNULLAccess++;
				}

				else{
					procTable[i].numIHTNonNULLAcess++;
					while(current != NULL){
						procTable[i].numIHTConflictAccess++;
						if(current->virtualPageNumber == VPN && current->pid ==i){
							//page hit
							// printf("*****hit*****\n");
							PFN = current->frameNumber;

							if(oldestFrame == &phyMemFrames[PFN])
								oldestFrame = oldestFrame->lruRight;

							else{
								phyMemFrames[PFN].lruLeft->lruRight = phyMemFrames[PFN].lruRight;
								phyMemFrames[PFN].lruRight->lruLeft = phyMemFrames[PFN].lruLeft;
								phyMemFrames[PFN].lruRight = oldestFrame;
								phyMemFrames[PFN].lruLeft = oldestFrame->lruLeft;
								oldestFrame->lruLeft->lruRight = &phyMemFrames[PFN];
								oldestFrame->lruLeft = &phyMemFrames[PFN];   
							}

							procTable[i].numPageHit++;
							break;
						}
						previous = current;
						current = current->next;
					}

					if(current == NULL){
						//page fault
						//newnode 할당하고 초기화, 지워야되는지 검사
						// printf("*****fault2*****\n");
						struct invertedPageTableEntry *newNode = (struct invertedPageTableEntry*)malloc(sizeof(struct invertedPageTableEntry));
						struct invertedPageTableEntry *first = invertedPageTable[HASH];
						newNode->pid = i;
						newNode->virtualPageNumber = VPN;
						newNode->frameNumber = oldestFrame->number;
						newNode->next = first;
						invertedPageTable[HASH] = newNode;

						if(oldestFrame->virtualPageNumberforFirst != -1){
							dHASH = (oldestFrame->virtualPageNumberforFirst + oldestFrame->pid)%nFrame;
							struct invertedPageTableEntry * dcurrent = invertedPageTable[dHASH];
							struct invertedPageTableEntry * pre_dcurrent = NULL;

							while(dcurrent != NULL){
								if(dcurrent->virtualPageNumber == oldestFrame->virtualPageNumberforFirst && dcurrent->pid == oldestFrame->pid && dcurrent->frameNumber == oldestFrame->number){
									if(pre_dcurrent == NULL) invertedPageTable[dHASH] = dcurrent->next;
									else pre_dcurrent->next = dcurrent->next;
									free(dcurrent);
									break;
								}
								pre_dcurrent = dcurrent;
								dcurrent = dcurrent->next;
							}
							// printf("*****\n");
						}

						PFN = newNode->frameNumber;
						oldestFrame->pid = i;
						oldestFrame->virtualPageNumberforFirst = VPN;
						oldestFrame = oldestFrame->lruRight;

						procTable[i].numPageFault++;

					}
				}
				Paddr = (PFN << 12) + offset; 
			}
			if (s_flag)		//-s option print statement
				printf("IHT procID %d traceNumber %d virtual addr %x physical addr %x\n", i, procTable[i].ntraces,Vaddr,Paddr );
		}
		if (count == numProcess)
			break;
	}
			
	


	// -s option print statement
	// printf("IHT procID %d traceNumber %d virtual addr %x physical addr %x\n", i, procTable[i].ntraces,Vaddr,Paddr);
	int i;
	for(i=0; i < numProcess; i++) {
		printf("**** %s *****\n",procTable[i].traceName);
		printf("Proc %d Num of traces %d\n",i,procTable[i].ntraces);
		printf("Proc %d Num of Inverted Hash Table Access Conflicts %d\n",i,procTable[i].numIHTConflictAccess);
		printf("Proc %d Num of Empty Inverted Hash Table Access %d\n",i,procTable[i].numIHTNULLAccess);
		printf("Proc %d Num of Non-Empty Inverted Hash Table Access %d\n",i,procTable[i].numIHTNonNULLAcess);
		printf("Proc %d Num of Page Faults %d\n",i,procTable[i].numPageFault);
		printf("Proc %d Num of Page Hit %d\n",i,procTable[i].numPageHit);
		assert(procTable[i].numPageHit + procTable[i].numPageFault == procTable[i].ntraces);
		assert(procTable[i].numIHTNULLAccess + procTable[i].numIHTNonNULLAcess == procTable[i].ntraces);
	}

	free(invertedPageTable);
}


int main(int argc, char *argv[]) {
	int i;
	int simType;

	if (argc < 3) {
	     printf("Usage : %s [-s] firstLevelBits PhysicalMemorySizeBits TraceFileNames\n",argv[0]); exit(1);
	}
	
	if (!strcmp(argv[1],"-s"))
		s_flag = 1;

	simType = atoi(argv[s_flag+1]);
	firstLevelBits = atoi(argv[s_flag+2]);
 	phyMemSizeBits = atoi(argv[s_flag+3]);
	

	if (phyMemSizeBits < PAGESIZEBITS) {
		printf("PhysicalMemorySizeBits %d should be larger than PageSizeBits %d\n",phyMemSizeBits,PAGESIZEBITS); exit(1);
	}
	if (VIRTUALADDRBITS - PAGESIZEBITS - firstLevelBits <= 0 ) {
		printf("firstLevelBits %d is too Big for the 2nd level page system\n",firstLevelBits); exit(1);
	}
	
	//아무 옵션도 입력하지 않았을때의 argc=1
	
	numProcess = argc-s_flag-4;

	// initialize procTable for memory simulations
	for(i = 0; i < numProcess; i++) {
		printf("process %d opening %s\n",i,argv[i+4+s_flag]);
	}
	
	int nFrame = (1<<(phyMemSizeBits-PAGESIZEBITS)); assert(nFrame>0);
	printf("\nNum of Frames %d Physical Memory Size %ld bytes\n",nFrame, (1L<<phyMemSizeBits));
	

	struct procEntry *procTable = (struct procEntry*)malloc(sizeof(struct procEntry)*numProcess);
	
	for(i=0; i< numProcess; i++){
		procTable[i].traceName = (char *)malloc(strlen(argv[i+4+s_flag])+1);	//1 for NULL
		strcpy(procTable[i].traceName, argv[i+4+s_flag]);
	}
	
	struct framePage *physicalMemory = (struct framePage*)malloc(sizeof(struct framePage)*nFrame);

	unsigned Vaddr, rw;
	if (simType == 0 || simType > 2){
		// initialize procTable for the simulation
		printf("=============================================================\n");
		printf("The One-Level Page Table with FIFO Memory Simulation Starts .....\n");
		printf("=============================================================\n");
		initPhyMem(physicalMemory, nFrame);
		initProcTab(procTable, numProcess);

		oneLevelVMSim(procTable, physicalMemory, 'F');

		for(i=0; i< numProcess; i++)
			rewind(procTable[i].tracefp);	//file pointer의 위치를 각 파일의 제일 처음으로
	
		// initialize procTable for the simulation
		printf("=============================================================\n");
		printf("The One-Level Page Table with LRU Memory Simulation Starts .....\n");
		printf("=============================================================\n");
		initPhyMem(physicalMemory, nFrame);
		initProcTab(procTable, numProcess);
		oneLevelVMSim(procTable, physicalMemory, 'L');
		// call oneLevelVMSim() with LRU

		for(i=0; i< numProcess; i++)
			rewind(procTable[i].tracefp);
	}	

	if(simType == 1 || simType > 2){
		// initialize procTable for the simulation
		printf("=============================================================\n");
		printf("The Two-Level Page Table Memory Simulation Starts .....\n");
		printf("=============================================================\n");
		// call twoLevelVMSim()
		initPhyMem(physicalMemory, nFrame);
		initProcTab(procTable, numProcess);

		twoLevelVMSim(procTable, firstLevelBits, physicalMemory); 

		for(i=0; i< numProcess; i++)
			rewind(procTable[i].tracefp);	//file pointer의 위치를 각 파일의 제일 처음으로
	}
	
	if(simType == 2 || simType > 2){
		// initialize procTable for the simulation
		printf("=============================================================\n");
		printf("The Inverted Page Table Memory Simulation Starts .....\n");
		printf("=============================================================\n");
		// call invertedPageVMsim()
		initPhyMem(physicalMemory, nFrame);
		initProcTab(procTable, numProcess);

		invertedPageVMSim(procTable, physicalMemory, nFrame);

		// invertedPageVMSim(procTable, physicalMemory); 

		for(i=0; i< numProcess; i++)
			rewind(procTable[i].tracefp);	//file pointer의 위치를 각 파일의 제일 처음으로
		
	}
	for(i=0; i< numProcess; i++)
			fclose(procTable[i].tracefp);	//file pointer의 위치를 각 파일의 제일 처음으로
	

	return(0);
}
