use crate::accounting::Account;
use std::hash::Hash;
use std::{collections::HashMap, fs::File};

pub struct Report {
    counts_writer: csv::Writer<File>,
    counts: HashMap<String, usize>,
    validation_writer: csv::Writer<File>,
    validations: HashMap<(String, String), (usize, usize, usize, usize)>,
    written: bool,
}

impl Report {
    pub fn new(counts_file: &str, validation_file: &str) -> csv::Result<Self> {
        let validation_writer = csv::WriterBuilder::new()
            .flexible(true)
            .from_path(validation_file)?;

        let counts_writer = csv::WriterBuilder::new()
            .flexible(true)
            .from_path(counts_file)?;

        Ok(Self {
            counts_writer,
            counts: Default::default(),
            validation_writer,
            validations: Default::default(),
            written: false,
        })
    }
    pub fn report_count(&mut self, name: impl ToString, count: usize) {
        self.counts.insert(name.to_string(), count);
    }

    pub fn report_all(&mut self, iter: impl IntoIterator<Item = (String, usize)>) {
        for (name, count) in iter {
            self.report_count(name, count);
        }
    }

    pub fn report_account<'a, A: Eq + Hash>(
        &mut self,
        a_name: impl ToString,
        b_name: impl ToString,
        account: Account<A>,
    ) {
        self.report_count(a_name.to_string(), account.a.len());
        self.report_count(b_name.to_string(), account.b.len());
        self.validations.insert(
            (a_name.to_string(), b_name.to_string()),
            (
                account.a.len(),
                account.b.len(),
                account.a_minus_b().count(),
                account.b_minus_a().count(),
            ),
        );
    }
    pub fn write(&mut self) -> csv::Result<()> {

        if self.written {
            return Ok(())
        }
        let mut sorted_counts = self.counts.iter().collect::<Vec<_>>();
        sorted_counts.sort_by(|(id1, _), (id2, _)| (&*id1.to_string()).cmp(&*id2.to_string()));


        for (name, count) in sorted_counts {
            self.counts_writer
                .write_record(&[name, &*count.to_string()])?;
        }

        let mut sorted_validations = self.validations.iter().collect::<Vec<_>>(); 
        sorted_validations.sort_by(|(k1, _), (k2, _)| k1.cmp(&k2));

        for ((a_name, b_name), (a_size, b_size, a_minus_b_size, b_minus_a_size)) in sorted_validations 
        {
            self.validation_writer.write_record(&[
                a_name,
                b_name,
                &*a_size.to_string(),
                &*b_size.to_string(),
                &*a_minus_b_size.to_string(),
                &*b_minus_a_size.to_string(),
            ])?;
        }
        self.written = true;
        Ok(())
    }
    pub fn flush(&mut self) -> csv::Result<()> {
        self.counts_writer.flush()?;
        self.validation_writer.flush()?;
        Ok(())
    }
}


