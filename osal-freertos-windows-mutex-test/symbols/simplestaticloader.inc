int32 stub1(void);
int32 stub2(void);

static_load_file_header_t known_symbols[] = {
		/* module name,                         entry point name,         entry point address, code address, code size, data address, data size, bss address, bss size, flags */
		{  "/ram/stub2.so",                  "stub1",                  (cpuaddr)&stub1,     0,            0,         0,            0,         0,           0,        0},
		{  "/ram/stub2.so",                  "stub2",                  (cpuaddr)&stub2,     0,            0,         0,            0,         0,           0,        0},
		{  "/ram/OS_Application_Startup.so", "OS_Application_Startup", (cpuaddr)&stub2,     0,            0,         0,            0,         0,           0,        0}
};

