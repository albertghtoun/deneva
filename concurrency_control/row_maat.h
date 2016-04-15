/*
   Copyright 2015 Rachael Harding

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef ROW_MAAT_H
#define ROW_MAAT_H

class Row_maat {
public:
	void init(row_t * row);
  RC read(TxnManager * txn);
  RC prewrite(TxnManager * txn);
  RC commit(TxnManager * txn);
  RC release(TxnManager * txn);
	
private:
  pthread_mutex_t latch;
	
	row_t * _row;
	
  std::set<uint64_t> uncommitted_reads;
  std::set<uint64_t> uncommitted_writes;
  uint64_t timestamp_last_read;
  uint64_t timestamp_last_write;
};

#endif
