'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const M=require('./port_model.cjs');
const clone=v=>JSON.parse(JSON.stringify(v));
function tool(){return {kind:'python_class',init:[],inputs:[{id:1,name:'阈值',type:'int'},{id:2,name:'图像',type:'image'},{id:3,name:'ROI',type:'roi'}],outputs:[{id:2,name:'图像',type:'image'}],next_port_id:{init:0,inputs:4,outputs:3}};}
const checks=[];
function test(name,run){run();checks.push(name);}
test('重排保持id与值对应',()=>{const t=tool(),v=new Map([[1,128],[2,'IMAGE'],[3,'ROI']]);M.move(t,'inputs',2,0);assert.deepEqual(t.inputs.map(x=>x.id),[2,1,3]);assert.equal(v.get(1),128);});
test('删除最大号后序列化重开不复用',()=>{const t=tool();M.remove(t,'inputs',3);const reopened=clone(t);const p=M.add(reopened,'inputs');assert.equal(p.id,4);assert.equal(reopened.next_port_id.inputs,5);});
test('输入保留号0拒绝',()=>{const t=tool();t.inputs[0].id=0;assert.throws(()=>M.validate(t));});
test('输出保留号1拒绝',()=>{const t=tool();t.outputs[0].id=1;assert.throws(()=>M.validate(t));});
test('初始化0合法且方向计数独立',()=>{const t=tool();assert.equal(M.add(t,'init').id,0);assert.equal(t.next_port_id.inputs,4);M.validate(t);});
test('同方向重复拒绝',()=>{const t=tool();t.inputs[2].id=2;assert.throws(()=>M.validate(t));});
test('字符串和布尔ID拒绝',()=>{for(const id of ['1',true]){const t=tool();t.inputs[0].id=id;assert.throws(()=>M.validate(t));}});
test('高编号不产生稠密填充',()=>{const t=tool();t.inputs=[{id:M.MAX,name:'高号',type:'int'}];t.next_port_id.inputs=M.MAX+1;M.validate(t);assert.equal(t.inputs.length,1);assert.throws(()=>M.add(t,'inputs'));});
test('复制保留参数编号和高水位',()=>{const t=tool();M.remove(t,'inputs',3);const copy=clone(t);assert.deepEqual(copy,t);assert.equal(M.add(copy,'inputs').id,4);});
test('名称相同不影响身份',()=>{const t=tool();t.inputs.forEach(p=>p.name='参数');M.validate(t);assert.equal(t.inputs[1].id,2);});
test('函数不接受初始化',()=>{const t=tool();t.kind='python_function';assert.throws(()=>M.add(t,'init'));});
test('不能删除不存在的固定输入',()=>{const t=tool();assert.throws(()=>M.remove(t,'inputs',0));assert.equal(t.next_port_id.inputs,4);});
const root=path.resolve(__dirname,'..');
const docs=['threshold.json','threshold_reordered.json','threshold_deleted_max_id.json','threshold_added_after_delete.json','all_types.json','counter_class.json'].map(n=>JSON.parse(fs.readFileSync(path.join(root,'examples',n),'utf8')));
test('新协议示例编号与高水位有效',()=>{for(const d of docs){assert.equal(d.schema_version,6);const t={kind:d.implementation.kind,next_port_id:d.next_port_id,init:d.ports.init||[],inputs:d.ports.inputs||[],outputs:d.ports.outputs||[]};M.validate(t);}});
test('示例排序删除新增与文档编号一致',()=>{assert.deepEqual(docs[1].ports.inputs.map(p=>p.id),[2,1,3]);assert.equal(docs[2].next_port_id.inputs,4);assert.equal(docs[3].ports.inputs.at(-1).id,4);});
test('全部34具体类型输入输出成对',()=>{const t=docs[4];assert.equal(t.ports.inputs.length,34);assert.equal(new Set(t.ports.inputs.map(p=>p.type)).size,34);for(let i=0;i<34;i++){assert.equal(t.ports.inputs[i].id,i+1);assert.equal(t.ports.outputs[i].id,i+2);assert.equal(t.ports.inputs[i].type,t.ports.outputs[i].type);}});
test('原型嵌入同一编号逻辑',()=>{const html=fs.readFileSync(path.join(root,'自定义工具_数字编号原型.html'),'utf8');assert(html.includes(fs.readFileSync(path.join(__dirname,'port_model.cjs'),'utf8')));assert(html.includes('参数编号'));assert(!html.includes('name_en:p.id'));});
const result={date:'2026-11-03',scope:'R45设计参考模型与示例检查，不是产品CTest或Qt验收',passed:checks.length,checks};
fs.mkdirSync(path.join(root,'evidence'),{recursive:true});fs.writeFileSync(path.join(root,'evidence','R45_design_checks.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({passed:checks.length}));
