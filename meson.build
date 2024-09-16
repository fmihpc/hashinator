project('Hashinator', 'cpp', 'cuda' ,default_options : ['cpp_std=c++20','warning_level=2','werror=true','buildtype=debugoptimized'])
 
#Config
add_global_arguments('-DHASHMAPDEBUG', language : 'cpp')
add_global_arguments('-DHASHMAPDEBUG', language : 'cuda')
add_project_arguments(['--expt-relaxed-constexpr','--expt-extended-lambda' ], language: 'cuda')

#Umpire
inc_umpire = include_directories('/home/kstppd/software/Umpire/build/include','/home/kstppd/software/Umpire/src/','/home/kstppd/software/Umpire/src/tpl/umpire/camp/include/','/home/kstppd/software/Umpire/build/src/tpl/umpire/camp/include/','/home/kstppd/software/Umpire/src/tpl/umpire/fmt/include/','/home/kstppd/software/Umpire/src/tpl/',is_system:true)
lib_umpire = include_directories('/home/kstppd/software/Umpire/build/lib',is_system:true)
umpire_dep = declare_dependency(
    link_args : ['-L' + '/home/kstppd/software/Umpire/build/lib', '-lumpire'],
    include_directories : inc_umpire)
# umpire_dep = dependency('umpire' ,required:false)
  
#Dependencies
gtest_dep = dependency('gtest', fallback : ['gtest', 'gtest_dep'])

#Limit register usage in debug builds
if ( get_option('buildtype') == 'debug')
  add_project_arguments('-maxrregcount=32',  language : 'cuda')
endif


#Unit tests
hashinator_unit = executable('hashmap_test', 'unit_tests/hashmap_unit_test/main.cu',dependencies :gtest_dep )
splitvector_device_unit = executable('splitvector_device_test', 'unit_tests/gtest_vec_device/vec_test.cu',dependencies :gtest_dep )
splitvector_host_unit = executable('splitvector_host_test', 'unit_tests/gtest_vec_host/vec_test.cu',dependencies :gtest_dep )
compaction_unit = executable('compaction_test', 'unit_tests/stream_compaction/race.cu',dependencies :gtest_dep )
compaction2_unit = executable('compaction2_test', 'unit_tests/stream_compaction/preallocated.cu', cuda_args:['--default-stream=per-thread','-Xcompiler','-fopenmp'],link_args : ['-fopenmp'],dependencies :gtest_dep)
compaction3_unit = executable('compaction3_test', 'unit_tests/stream_compaction/unit.cu', cuda_args:'--default-stream=per-thread',link_args : ['-fopenmp'],dependencies :gtest_dep)
pointer_unit = executable('pointer_test', 'unit_tests/pointer_test/main.cu',dependencies :gtest_dep )
hybridCPU = executable('hybrid_cpu', 'unit_tests/hybrid/main.cu',cpp_args:'-DHASHINATOR_CPU_ONLY_MODE',dependencies :gtest_dep )
hashinator_bench = executable('bench', 'unit_tests/benchmark/main.cu', dependencies :gtest_dep,link_args:'-lnvToolsExt')
compaction_bench = executable('streamBench', 'unit_tests/stream_compaction/bench.cu' ,link_args:'-lnvToolsExt')
deletion_mechanism = executable('deletion', 'unit_tests/delete_by_compaction/main.cu', dependencies :gtest_dep)
insertion_mechanism = executable('insertion', 'unit_tests/insertion_mechanism/main.cu', dependencies :gtest_dep)
tombstoneTest = executable('tbPerf', 'unit_tests/benchmark/tbPerf.cu', dependencies :gtest_dep)
realisticTest = executable('realistic', 'unit_tests/benchmark/realistic.cu', dependencies :gtest_dep)
hybridGPU = executable('hybrid_gpu', 'unit_tests/hybrid/main.cu',dependencies :gtest_dep )

hashinator_umpire_unit = executable('hashmap_umpire_test', 'unit_tests/umpire_tests/hashinator.cu',dependencies :[umpire_dep,gtest_dep], include_directories:inc_umpire)
splitvector_host_umpire_unit = executable('splitvector_host_umpire_test', 'unit_tests/umpire_tests/vec_host.cu',dependencies :[umpire_dep,gtest_dep], include_directories:inc_umpire )
splitvector_device_umpire_unit = executable('splitvector_device_umpire_test', 'unit_tests/umpire_tests/vec_device.cu',dependencies :[umpire_dep,gtest_dep], include_directories:inc_umpire )

#Test-Runner
test('HashinatorTest', hashinator_unit)
test('SplitVectorDeviceTest', splitvector_device_unit)
test('SplitVectorHostTest', splitvector_host_unit)
test('CompactionTest',  compaction_unit)
test('CompactionTest2',  compaction2_unit)
test('CompactionTest3',  compaction3_unit)
test('HashinatorBench',  hashinator_bench)
test('CompactionBench',  compaction_bench)
test('Insertion',  insertion_mechanism)
test('Deletion',  deletion_mechanism)
test('PointerTest',  pointer_unit)
test('hybridCPU_Test',  hybridCPU)
test('hybridGPU_Test',  hybridGPU)
test('TbTest',  tombstoneTest)
test('RealisticTest',  realisticTest)
