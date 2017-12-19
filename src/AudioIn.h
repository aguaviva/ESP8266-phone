typedef short (*GetSampleFn)();

short *aiLock();
void aiUnlock();
void aiBegin(GetSampleFn pfn, unsigned int sampleRate);
void aiEnd();
