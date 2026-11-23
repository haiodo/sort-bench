use std::{
    env, fs,
    time::{Duration, Instant},
};

fn benchmark<const STABLE: bool>(source: &[i32], shape: &str, reps: usize) {
    let mut work = source.to_vec();
    let mut expected = source.to_vec();
    expected.sort_unstable();
    let sort_once = |work: &mut [i32]| {
        let start = Instant::now();
        if STABLE {
            work.sort();
        } else {
            work.sort_unstable();
        }
        std::hint::black_box(&*work);
        start.elapsed()
    };
    let warm = Instant::now();
    let pilot = loop {
        work.copy_from_slice(source);
        let pilot = sort_once(&mut work);
        if warm.elapsed() >= Duration::from_millis(300) {
            break pilot;
        }
    };
    let batch = if pilot.is_zero() {
        1
    } else {
        (0.2 / pilot.as_secs_f64()).ceil().clamp(1.0, 20000.0) as usize
    };
    let name = if STABLE { "sort" } else { "sort_unstable" };
    let n = source.len();
    for rep in 1..=reps {
        let mut total = Duration::ZERO;
        for _ in 0..batch {
            work.copy_from_slice(source);
            total += sort_once(&mut work);
        }
        assert!(work.is_sorted() && work == expected);
        let ms = total.as_secs_f64() * 1000.0 / batch as f64;
        let ns_el = ms * 1e6 / n as f64;
        println!(
            r#"{{"lang":"rust","impl":"{name}","type":"i32","dist":"{shape}","n":{n},"rep":{rep},"ms":{ms:.4},"ns_el":{ns_el:.3},"ok":true}}"#
        );
    }
}

fn main() {
    let args: Vec<_> = env::args().collect();
    assert!(
        args.len() == 7,
        "usage: rustsort IMPL i32 SHAPE FILE N REPS"
    );
    assert_eq!(args[2], "i32");
    assert!(
        ["random", "sorted", "reverse", "nearly", "few", "equal", "organ", "saw", "small"]
            .contains(&args[3].as_str())
    );
    let n = args[5].parse::<usize>().unwrap();
    let reps = args[6].parse::<usize>().unwrap();
    assert!(n > 0 && reps > 0);
    let data = fs::read(&args[4]).unwrap();
    assert_eq!(data.len(), n.checked_mul(4).unwrap());
    let source: Vec<_> = data
        .chunks_exact(4)
        .map(|v| i32::from_le_bytes(v.try_into().unwrap()))
        .collect();
    match args[1].as_str() {
        "sort" => benchmark::<true>(&source, &args[3], reps),
        "sort_unstable" => benchmark::<false>(&source, &args[3], reps),
        _ => panic!("unknown sort implementation"),
    }
}
