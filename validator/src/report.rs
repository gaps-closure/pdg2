use crate::accounting::Account;
use crate::bag::Bag;
use std::fmt::Display;
use std::hash::Hash;
use std::{collections::HashMap, fs::File};


pub struct Report {
    counts_writer: Option<csv::Writer<File>>,
    counts: HashMap<String, Option<usize>>,
    validation_writer: csv::Writer<File>,
    validations: HashMap<(String, String), Option<(usize, usize, usize, usize)>>,
    accounts_writer: csv::Writer<File>, 
    accounts: Bag<(String, String), Box<dyn Display>>, 
    counts_ordering: HashMap<String, usize>,
    validation_ordering: HashMap<(String, String), usize>,
    written: bool,
}

impl Report {
    pub fn new(counts_file: Option<&str>, validation_file: &str, accounts_file: &str) -> csv::Result<Self> {
        let validation_writer = csv::WriterBuilder::new()
            .flexible(true)
            .from_path(validation_file)?;

        let accounts_writer = csv::WriterBuilder::new()
            .flexible(true)
            .from_path(accounts_file)?;


        let counts_writer =
            match counts_file.map(|f| csv::WriterBuilder::new().flexible(true).from_path(f)) {
                Some(x) => Some(x?),
                None => None,
            };
        Ok(Self {
            counts_writer,
            counts: Default::default(),
            validation_writer,
            validations: Default::default(),
            accounts_writer,
            accounts: Bag::new(),
            counts_ordering: Default::default(),
            validation_ordering: Default::default(),
            written: false,
        })
    }

    fn write_headers(&mut self) -> csv::Result<()> {
        match &mut self.counts_writer {
            Some(writer) => writer.write_record(["Category", "Count"])?,
            None => (),
        };
        self.validation_writer
            .write_record(["A", "B", "|A|", "|B|", "|A-B|", "|B-A|"])?;
    
        self.accounts_writer
            .write_record(["A", "B", "Element of A - B"])?;
        Ok(())
    }
    pub fn report_count(&mut self, name: impl ToString, count: usize) {
        self.counts.insert(name.to_string(), Some(count));
    }

    pub fn report_all(&mut self, iter: impl IntoIterator<Item = (String, usize)>) {
        for (name, count) in iter {
            self.report_count(name, count);
        }
    }

    pub fn report_account<'a, A: Eq + Hash + Display + Clone + 'static>(
        &mut self,
        a_name: impl ToString,
        b_name: impl ToString,
        account: Account<A>,
    ) {
        self.report_count(a_name.to_string(), account.a.len());
        self.report_count(b_name.to_string(), account.b.len());
        self.validations.insert(
            (a_name.to_string(), b_name.to_string()),
            Some((
                account.a.len(),
                account.b.len(),
                account.a_minus_b().count(),
                account.b_minus_a().count(),
            )),
        );

        for x in account.a_minus_b() {
            self.accounts.insert((a_name.to_string(), b_name.to_string()), Box::new(x.clone()));
        }

        for x in account.b_minus_a() {
            self.accounts.insert((b_name.to_string(), a_name.to_string()), Box::new(x.clone()));
        }

    }

    pub fn set_counts_ordering(&mut self, ordering: HashMap<String, usize>) {
        self.counts_ordering = ordering;

        for (entry, _) in &self.counts_ordering {
            if !self.counts.contains_key(entry) {
                self.counts.insert(entry.clone(), None);
            }
        }
    }

    pub fn set_validations_ordering(&mut self, ordering: HashMap<(String, String), usize>) {
        self.validation_ordering = ordering;

        for (entry, _) in &self.validation_ordering {
            if !self.validations.contains_key(&entry) {
                self.validations.insert(entry.clone(), None);
            }
        }
    }

    pub fn write(&mut self) -> csv::Result<()> {
        if self.written {
            return Ok(());
        }
        self.write_headers()?;
        let mut sorted_counts = self.counts.iter().collect::<Vec<_>>();
        sorted_counts.sort_by(|(id1, _), (id2, _)| self.counts_ordering.get(*id1).cmp(&self.counts_ordering.get(*id2)));

        match &mut self.counts_writer {
            Some(writer) => {
                for (name, count) in sorted_counts {
                    match count {
                        Some(count) => writer.write_record(&[name, &*count.to_string()])?,
                        None => writer.write_record(&[name, "N/A"])?,
                    }
                }
            }
            None => (),
        }

        let mut sorted_validations = self.validations.iter().collect::<Vec<_>>();
        sorted_validations.sort_by(|(k1, _), (k2, _)| self.validation_ordering.get(*k1).cmp(&self.validation_ordering.get(*k2)));

        for ((a_name, b_name), validation) in sorted_validations {
            match validation {
                Some((a_size, b_size, a_minus_b_size, b_minus_a_size)) => {
                    self.validation_writer.write_record(&[
                        a_name,
                        b_name,
                        &*a_size.to_string(),
                        &*b_size.to_string(),
                        &*a_minus_b_size.to_string(),
                        &*b_minus_a_size.to_string(),
                    ])?
                }
                None => self
                    .validation_writer
                    .write_record(&[a_name, b_name, "N/A", "N/A", "N/A", "N/A"])?,
            }
        }

        for ((a_name, b_name), b) in self.accounts.iter() {
            let x = format!("{}", b);
            self.accounts_writer.write_record([a_name, b_name, &x])?;
        }
        self.written = true;
        Ok(())
    }
    pub fn flush(&mut self) -> csv::Result<()> {
        match &mut self.counts_writer {
            Some(writer) => writer.flush()?,
            None => (),
        }
        self.validation_writer.flush()?;
        Ok(())
    }
}
