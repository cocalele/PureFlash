int ec_write_data(PfServerIocb* iocb, PfVolume* vol)
{
	iocb->add_ref(); //dec_ref after aof write complete
	int64_t new_aof_off=0;


	//blabla, do aof logic here

	if (spdk_engine_used())
		((PfSpdkQueue*)(app_context.disps[iocb->disp_index]->event_queue))->post_event_locked(EVT_EC_UPDATE_LUT, 0, iocb, new_aof_off);
	else
		app_context.disps[iocb->disp_index]->event_queue->post_event(EVT_EC_UPDATE_LUT, 0, iocb, new_aof_off); //for read
	iocb->dec_ref(); //write completed
	return 0;
}
