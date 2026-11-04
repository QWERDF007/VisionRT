(function(root){
'use strict';
const MAX=4294967295, MIN={init:0,inputs:1,outputs:2};
function checkStage(stage){if(!Object.hasOwn(MIN,stage))throw Error('参数方向不合法');}
function validate(t){
 for(const stage of Object.keys(MIN)){
  checkStage(stage);const next=t.next_port_id?.[stage];if(!Number.isSafeInteger(next)||next<MIN[stage]||next>MAX+1)throw Error('next_port_id 不合法');
  const seen=new Set();for(const p of t[stage]){if(!Number.isInteger(p.id)||p.id<MIN[stage]||p.id>MAX||seen.has(p.id))throw Error('参数编号重复、保留或越界');seen.add(p.id);if(p.id>=next)throw Error('next_port_id 必须大于已有编号');}
 }
 if(t.kind==='python_function'&&t.init.length)throw Error('函数工具不接受初始化参数');
 return true;
}
function add(t,stage){checkStage(stage);validate(t);if(stage==='init'&&t.kind==='python_function')throw Error('函数工具不接受初始化参数');const id=t.next_port_id[stage];if(id>MAX)throw Error('参数编号已耗尽');const p={id,name:'参数 '+id,type:'float',value:'0',committed:false};t[stage].push(p);t.next_port_id[stage]++;return p;}
function remove(t,stage,id){checkStage(stage);const i=t[stage].findIndex(p=>p.id===id);if(i<0)throw Error('不能删除固定参数或不存在的参数');t[stage].splice(i,1);}
function move(t,stage,id,to){checkStage(stage);const a=t[stage],from=a.findIndex(p=>p.id===id);if(from<0||to<0||to>=a.length)throw Error('移动位置不合法');if(from!==to)a.splice(to,0,a.splice(from,1)[0]);}
const api={MAX,MIN,validate,add,remove,move};if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.VisionRTPorts=api;
})(globalThis);
