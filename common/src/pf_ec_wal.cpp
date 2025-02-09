PfEcRedologEntry* PfEcRedolog::alloc_entry()
{
	
	PfEcRedologEntry* e = &current_page->entries[current_page->free_index++];
	if(current_page->free_index >= ENTRY_PER_PAGE){
		
	}
}


void PfEcRedolog::commit_entry(PfEcRedologEntry* e)
{
	PfEcRedologPage* page = PAGE_OF_WAL(e);

}