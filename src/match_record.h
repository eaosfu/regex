#ifndef MATCH_RECORD_H_
#define MATCH_RECORD_H_

typedef struct MatchRecord {
  char * beg;
  char * end;
  struct MatchRecord * next;
  struct MatchRecord * prev;
} MatchRecord;


typedef struct MatchRecordObj {
  MatchRecord * head;
  int match_count;
  MatchRecord * pool;
} MatchRecordObj;


MatchRecordObj * new_match_record_obj();
void  new_match_record(MatchRecordObj *, char *, char *);
void match_record_release(MatchRecord *);
void match_record_free(MatchRecordObj **);
void match_record_clear(MatchRecordObj *);

#define MATCH_RECORD_COUNT(o) ((o == NULL) ? 0 : o->match_count)

#endif
