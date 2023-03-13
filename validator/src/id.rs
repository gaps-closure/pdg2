#[derive(Debug,PartialEq,Eq,Hash,Clone)]
pub struct ID(pub Vec<String>);

#[macro_export]
macro_rules! id {
    ($($x:ident).+) => {
        ID(vec![$(stringify!{$x}.to_string()),+])
    };
    ($($e:expr),+) => {
        ID(vec![$($e.to_string()),+])
    };
}

#[macro_export]
macro_rules! ids {
    ($($($x:ident).+),+) => {
        vec![$(id!($($x).+)),+]
    };
}

impl ID {
    pub fn prefix(&self, pfx: usize) -> Self {
        ID(self.0[0..pfx].to_owned())
    } 
    pub fn len(&self) -> usize {
        self.0.len()
    }
    pub fn right_extend(&self, ext: String) -> Self {
        ID([self.0.clone(), vec![ext]].concat())
    }
}

impl ToString for ID {
    fn to_string(&self) -> String {
        self.0.join(".")
    }
}

