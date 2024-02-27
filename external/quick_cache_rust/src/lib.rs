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
