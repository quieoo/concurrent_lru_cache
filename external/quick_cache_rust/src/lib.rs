// lib.rs
use cacache;

// // Use the synchronous cache.
// use moka::sync::Cache;

// use std::thread;

// fn value(n: usize) -> String {
//     format!("value {n}")
// }

// #[no_mangle]
// pub extern "C" fn test_moka() {
//     const NUM_THREADS: usize = 16;
//     const NUM_KEYS_PER_THREAD: usize = 64;

//     // Create a cache that can store up to 10,000 entries.
//     let cache = Cache::new(10_000);
//     println!("created cache");
//     // Spawn threads and read and update the cache simultaneously.
//     let threads: Vec<_> = (0..NUM_THREADS)
//         .map(|i| {
//             // To share the same cache across the threads, clone it.
//             // This is a cheap operation.
//             let my_cache = cache.clone();
//             let start = i * NUM_KEYS_PER_THREAD;
//             let end = (i + 1) * NUM_KEYS_PER_THREAD;

//             thread::spawn(move || {
//                 // Insert 64 entries. (NUM_KEYS_PER_THREAD = 64)
//                 for key in start..end {
//                     my_cache.insert(key, value(key));
//                     // get() returns Option<String>, a clone of the stored value.
//                     assert_eq!(my_cache.get(&key), Some(value(key)));
//                 }

//                 // Invalidate every 4 element of the inserted entries.
//                 for key in (start..end).step_by(4) {
//                     my_cache.invalidate(&key);
//                 }
//             })
//         })
//         .collect();

//     // Wait for all threads to complete.
//     threads.into_iter().for_each(|t| t.join().expect("Failed"));

//     // Verify the result.
//     for key in 0..(NUM_THREADS * NUM_KEYS_PER_THREAD) {
//         if key % 4 == 0 {
//             assert_eq!(cache.get(&key), None);
//         } else {
//             assert_eq!(cache.get(&key), Some(value(key)));
//         }
//     }
// }




#[no_mangle]
pub extern "C" fn add(a: i32, b: i32) -> i32 {
    a + b
}

#[no_mangle]
pub extern "C" fn test_cacache() {
    

    cacache::write_sync("./my-cache", "key", b"my-data").unwrap();
    let data = cacache::read_sync("./my-cache", "key").unwrap();
    assert_eq!(data, b"my-data");
    println!("data: {:?}", data);
    //字符串格式打印data
    println!("data: {}", String::from_utf8(data).unwrap());
}

use quick_cache::unsync::Cache;

#[no_mangle]
pub extern "C" fn test_quick_cache() {
    let mut cache = Cache::new(5);
    cache.insert("square", "blue");
    cache.insert("circle", "black");
    assert_eq!(*cache.get(&"square").unwrap(), "blue");
    assert_eq!(*cache.get(&"circle").unwrap(), "black");
    println!("get: {:?}", cache.get(&"square"));
    println!("get: {:?}", cache.get(&"circle"));
}

#[no_mangle]
pub extern "C" fn test_quick_cache_table() {
    // create a cache with Key type as uint64_t and Value type a fixed-size array
    let mut cache = Cache::<u64, [u8; 8]>::new(5);
    cache.insert(1, [1, 2, 3, 4, 5, 6, 7, 8]);
    println!("get: {:?}", cache.get(&1));
    
}


use std::mem;

#[no_mangle]
//create a cache and return as a void pointer in C
pub extern "C" fn build_quick_cache(entry_num: u64)->*mut std::ffi::c_void {
    //创建cache，指定key和Value的类型为u64
    let cache_box = Box::new(Cache::<u64, u64>::new(entry_num as usize));
    // mem::forget(cache_box); // Prevents the destructor of Cache from being called
    let raw_ptr = Box::into_raw(cache_box) as *mut std::ffi::c_void;
    raw_ptr
}

#[no_mangle]
pub extern "C" fn quick_cache_insert(cache_ptr: *mut std::ffi::c_void, key: u64, value: u64) {
    let mut cache = unsafe { Box::from_raw(cache_ptr as *mut Cache<u64, u64>) };
    cache.insert(key, value);
    mem::forget(cache);
}

#[no_mangle]
pub extern "C" fn quick_cache_query(cache_ptr: *mut std::ffi::c_void, key: u64) -> u64 {
    let cache = unsafe { Box::from_raw(cache_ptr as *mut Cache<u64, u64>) };
    let ret=cache.get(&key);
    // mem::forget(cache);
    let result= if ret.is_none() {
        0
    }else{
        ret.unwrap().clone()
    };
    Box::leak(cache);
    result
}


#[repr(C)]
#[derive(Clone)]
pub struct R_PhysicalAddr{
    pub data: [u8;20],
}

#[no_mangle]
//create a cache and return as a void pointer in C
pub extern "C" fn build_quick_table_cache(entry_num: u64)->*mut std::ffi::c_void {
    //创建cache，指定key和Value的类型为u64
    let cache_box = Box::new(Cache::<u64, Vec<R_PhysicalAddr>>::new(entry_num as usize));
    // mem::forget(cache_box); // Prevents the destructor of Cache from being called
    let raw_ptr = Box::into_raw(cache_box) as *mut std::ffi::c_void;
    raw_ptr
}

#[no_mangle]
pub extern "C" fn quick_table_cache_insert(cache_ptr: *mut std::ffi::c_void, table_id: u64, table_data: *mut R_PhysicalAddr, table_len: u64) {
    
    //create a Vec with table_data
    let value = unsafe {
        Vec::from_raw_parts(table_data, table_len as usize, table_len as usize)
    };

    // restore cache
    let mut cache = unsafe { Box::from_raw(cache_ptr as *mut Cache<u64, Vec<R_PhysicalAddr>>) };
    //insert table into cache
    cache.insert(table_id, value);
    mem::forget(cache);
}

#[no_mangle]
pub extern "C" fn quick_table_cache_get(cache_ptr: *mut std::ffi::c_void, table_id: u64, table_offset: u64, result: *mut R_PhysicalAddr) {
    let cache = unsafe { Box::from_raw(cache_ptr as *mut Cache<u64, Vec<R_PhysicalAddr>>) };

    let ret = cache.get(&table_id);
    let result_struct: R_PhysicalAddr;

    if ret.is_none() {
        // return a R_PhysicalAddr with all 0xff
        result_struct = R_PhysicalAddr { data: [0xff; 20] };
    } else {
        // get element at table_offset
        result_struct = ret.unwrap()[table_offset as usize].clone();
    }

    // Copy the result_struct to the provided memory location
    unsafe {
        std::ptr::copy_nonoverlapping(&result_struct, result, 1);
    }

    // Leak the cache to avoid deallocating it
    Box::leak(cache);
}