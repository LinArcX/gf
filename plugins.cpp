#include <ctype.h>
#include <string.h>

int ASMRowMessage(UIElement *element, UIMessage message, int di, void *dp)
{
  if (message == UI_MSG_PAINT) {
    char *text = (char *)element->cp;
    if (!text) return 0;

    UIPainter *painter = (UIPainter *)dp;
    UIRectangle bounds = element->bounds;

    // Choose color
    uint32_t color = ui.theme.text; // default

    if (strstr(text, "=>")) {
      color = 0xFF66CC66; // Green for current instruction
    } else if (strstr(text, "0x")) {
      color = 0xFFAAAAAA; // Gray for assembly instructions
    } else {
      color = 0xFFFFFFFF; // White for c instructions
    }

    // Draw the text (this overrides the child UILabel's default color)
    UIDrawString(painter, bounds, text, -1, color, UI_ALIGN_LEFT, NULL);

    return 1; // Handled painting
  } 
  else if (message == UI_MSG_DEALLOCATE) {
    if (element->cp) {
      UI_FREE(element->cp);
      element->cp = NULL;
    }
  }
  return 0;
}

UIElement* ASMWindowCreate(UIElement *parent) 
{
  UIPanel *panel = UIPanelCreate(parent, UI_PANEL_SMALL_SPACING | UI_PANEL_COLOR_1 | UI_PANEL_SCROLL);
  return &panel->e;
}

void ASMWindowUpdate(const char *data, UIElement *panel)
{
  EvaluateCommand("disassemble /m $pc-128,$pc+128");  // Wider range for context

  if (strstr(evaluateResult, "No registers.") || 
    strstr(evaluateResult, "The current thread has terminated")) {
    return;
  }

  UIElementDestroyDescendents(panel);
  int currentRowIndex = -1;
  int rowIndex = 0;

  char *line = strtok(evaluateResult, "\n");
  while (line != NULL) {
    // Skip empty lines or GDB noise
    while (isspace(*line)) line++;
    if (*line == '\0' || strstr(line, "Dump of assembler code") || strstr(line, "End of assembler dump")) {
      line = strtok(NULL, "\n");
      continue;
    }

    UIPanel *row = UIPanelCreate(panel, UI_PANEL_HORIZONTAL | UI_ELEMENT_H_FILL | UI_PANEL_SMALL_SPACING);

    // Attach custom painter
    row->e.flags |= UI_ELEMENT_H_FILL;
    row->e.bounds.b = row->e.bounds.t + 20;
    row->e.messageUser = ASMRowMessage;

    // Copy line for the message handler (freed on deallocate)
    row->e.cp = UIStringCopy(line, -1);

    // Create the visible label (it will be overridden by our paint handler)
    UILabelCreate(&row->e, 0, " ", -1);

    if (strstr(line, "=>") && currentRowIndex == -1) {
      currentRowIndex = rowIndex;
    }
    rowIndex++;
    line = strtok(NULL, "\n");
  }

  // Auto-scroll to the current instruction line
  if (currentRowIndex >= 0 && panel->children && panel->childCount > 0) {
    UIPanel *scrollPanel = (UIPanel *)panel;
    if (scrollPanel->scrollBar) {
      int rowHeight = 20 * panel->window->scale; // Approximate row height (adjust if needed)
      int rowY = currentRowIndex * rowHeight;
      int visibleHeight = UI_RECT_HEIGHT(panel->bounds) - 40 * panel->window->scale;
      int targetScroll = rowY + visibleHeight / 3; //+ rowHeight / 2;
      if (targetScroll < 0) targetScroll = 0;
      // Center the line roughly
      scrollPanel->scrollBar->position = targetScroll;
    }
  }
  UIElementRefresh(panel);
}

__attribute__((constructor))
void ASMPluginRegister() {
    interfaceWindows.Add({ "ASM", ASMWindowCreate, ASMWindowUpdate });
}
