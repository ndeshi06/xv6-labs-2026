#include "types.h"
#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "defs.h"

struct bt_entry {
  uint64 address;
  uint function_id;
  uint file_id;
  uint line;
};

#include "backtrace_map.inc"

// Find the source information for a return address.
static const struct bt_entry *
lookup_backtrace(uint64 address)
{
  uint left = 0;
  uint right = BT_ENTRY_COUNT;

  while (left < right) {
    uint mid = left + (right - left) / 2;

    if (bt_entries[mid].address < address)
      left = mid + 1;
    else
      right = mid;
  }

  if (left < BT_ENTRY_COUNT &&
      bt_entries[left].address == address)
    return &bt_entries[left];

  return 0;
}

void
backtrace(void)
{
  uint64 fp = r_fp();
  uint64 stack_bottom = PGROUNDDOWN(r_sp());
  uint64 stack_top = stack_bottom + PGSIZE;

  printk("backtrace:\n");

  while (fp >= stack_bottom + 2 * sizeof(uint64) &&
         fp < stack_top) {
    uint64 ra = *(uint64 *)(fp - sizeof(uint64));
    uint64 previous_fp = *(uint64 *)(fp - 2 * sizeof(uint64));

    const struct bt_entry *entry = lookup_backtrace(ra);

    if (entry != 0) {
      printk("  %s - %s:%d\n",
             bt_functions[entry->function_id],
             bt_files[entry->file_id],
             entry->line);
    } else {
      printk("  %p (unknown)\n", (void *)ra);
    }

    if (previous_fp <= fp)
      break;

    fp = previous_fp;
  }
}
